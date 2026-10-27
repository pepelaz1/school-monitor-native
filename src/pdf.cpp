#include "model.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace monitor {
namespace {
std::vector<unsigned> decode(const std::string &s) {
  std::vector<unsigned> out;
  for (size_t i = 0; i < s.size();) {
    unsigned c = static_cast<unsigned char>(s[i++]);
    if (c < 128) {
      out.push_back(c);
      continue;
    }
    int n = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
    unsigned cp = c & ((1u << (6 - n)) - 1);
    if (!n) {
      out.push_back('?');
      continue;
    }
    bool ok = true;
    for (int k = 0; k < n; k++) {
      if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63);
    }
    out.push_back(ok && cp <= 65535 ? cp : '?');
  }
  return out;
}
std::string number(double x) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o << std::fixed << std::setprecision(2) << x;
  return o.str();
}
struct Font {
  const std::vector<unsigned char> &b = fontBytes();
  size_t cmap = 0, hmtx = 0;
  unsigned units = 1000, nmetrics = 0;
  std::set<unsigned> used;
  unsigned u16(size_t p) const {
    return (b.at(p) << 8) | b.at(p + 1);
  }
  unsigned u32(size_t p) const {
    return (u16(p) << 16) | u16(p + 2);
  }
  size_t table(const std::string &tag) {
    for (unsigned n = 0; n < u16(4); n++) {
      size_t p = 12 + n * 16;
      if (std::string(reinterpret_cast<const char *>(&b[p]), 4) == tag)
        return u32(p + 8);
    }
    throw std::runtime_error("TTF table not found");
  }
  Font() {
    units = u16(table("head") + 18);
    nmetrics = u16(table("hhea") + 34);
    hmtx = table("hmtx");
    auto t = table("cmap");
    for (unsigned i = 0; i < u16(t + 2); i++) {
      auto p = t + 4 + i * 8;
      auto sub = t + u32(p + 4);
      if (u16(sub) == 4)
        cmap = sub;
    }
    if (!cmap)
      throw std::runtime_error("TTF cmap missing");
  }
  unsigned glyph(unsigned c) const {
    unsigned n = u16(cmap + 6) / 2;
    size_t ends = cmap + 14, starts = ends + 2 * n + 2, deltas = starts + 2 * n,
           ranges = deltas + 2 * n;
    for (unsigned i = 0; i < n; i++) {
      if (c > u16(ends + 2 * i))
        continue;
      if (c < u16(starts + 2 * i))
        return 0;
      auto d = u16(deltas + 2 * i), r = u16(ranges + 2 * i);
      if (!r)
        return (c + d) & 65535;
      auto g = u16(ranges + 2 * i + r + 2 * (c - u16(starts + 2 * i)));
      return g ? (g + d) & 65535 : 0;
    }
    return 0;
  }
  unsigned width(unsigned c) const {
    return static_cast<unsigned>(
        std::lround(u16(hmtx + 4 * std::min(glyph(c), nmetrics - 1)) * 1000.0 / units));
  }
  double measure(const std::vector<unsigned> &s, double size) const {
    double r = 0;
    for (auto c : s)
      r += width(c) * size / 1000;
    return r;
  }
};
struct Pdf {
  Font font;
  std::vector<std::string> pages;
  std::string page;
  double y = 797;
  void fresh() {
    if (!page.empty())
      pages.push_back(page);
    page.clear();
    y = 797;
  }
  void space(double h) {
    if (y - h < 45)
      fresh();
  }
  void text(double x, double yy, const std::vector<unsigned> &cps, double size = 10) {
    std::ostringstream hex;
    hex << std::hex << std::uppercase << std::setfill('0');
    for (auto cp : cps) {
      font.used.insert(cp);
      hex << std::setw(4) << cp;
    }
    page += "0 0.102 0.204 rg BT /F1 " + number(size) + " Tf " + number(x) + " " + number(yy) +
            " Td <" + hex.str() + "> Tj ET\n";
  }
  void line(const std::string &s, double size = 10, double indent = 0) {
    auto cps = decode(s);
    std::vector<unsigned> row, word;
    auto emit = [&] {
      space(size + 8);
      text(45 + indent, y, row, size);
      y -= size + 7;
      row.clear();
    };
    auto place = [&] {
      if (word.empty())
        return;
      double extra = row.empty() ? 0 : font.width(' ') * size / 1000;
      if (!row.empty() && font.measure(row, size) + extra + font.measure(word, size) > 505 - indent)
        emit();
      if (!row.empty())
        row.push_back(' ');
      for (auto c : word) {
        if (!row.empty() && font.measure(row, size) + font.width(c) * size / 1000 > 505 - indent)
          emit();
        row.push_back(c);
      }
      word.clear();
    };
    for (auto c : cps) {
      if (c == ' ' || c == '\n') {
        place();
        if (c == '\n')
          emit();
      } else
        word.push_back(c);
    }
    place();
    if (!row.empty() || cps.empty())
      emit();
  }
  void title(const std::string &s) {
    space(42);
    y -= 8;
    line(s, 14);
    y -= 5;
  }
  void tableRow(const std::vector<std::string> &cells, const std::vector<double> &widths,
                bool head = false) {
    space(24);
    double x = 45;
    if (head)
      page += "0.898 0.937 1 rg 45 " + number(y - 7) + " 505 23 re f\n";
    for (size_t i = 0; i < cells.size(); i++) {
      auto cps = decode(cells[i]);
      while (font.measure(cps, 9) > widths[i] - 10 && !cps.empty())
        cps.pop_back();
      text(x + 4, y, cps, 9);
      x += widths[i];
    }
    page += "0.85 0.89 0.94 RG 0.5 w 45 " + number(y - 8) + " m 550 " + number(y - 8) + " l S\n";
    y -= 24;
  }
  void save(const std::filesystem::path &path) {
    fresh();
    std::vector<std::string> objects(7);
    objects[1] = "<< /Type /Catalog /Pages 2 0 R >>";
    std::string gids(131072, '\0'),
        widths = "[",
        map = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << "
              "/Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def /CMapName /SchoolUnicode def "
              "/CMapType 2 def 1 begincodespacerange <0000> <FFFF> endcodespacerange\n";
    std::vector<unsigned> used(font.used.begin(), font.used.end());
    for (auto c : used) {
      auto g = font.glyph(c);
      gids[c * 2] = static_cast<char>(g >> 8);
      gids[c * 2 + 1] = static_cast<char>(g & 255);
      widths += std::to_string(c) + " [" + std::to_string(font.width(c)) + "] ";
    }
    widths += "]";
    for (size_t i = 0; i < used.size(); i += 100) {
      size_t end = std::min(used.size(), i + 100);
      map += std::to_string(end - i) + " beginbfchar\n";
      for (size_t j = i; j < end; j++) {
        std::ostringstream h;
        h << std::hex << std::setfill('0') << std::setw(4) << used[j];
        map += "<" + h.str() + "> <" + h.str() + ">\n";
      }
      map += "endbfchar\n";
    }
    map += "endcmap CMapName currentdict /CMap defineresource pop end end";
    auto stream = [](const std::string &s, const std::string &extra = "") {
      return "<< /Length " + std::to_string(s.size()) + extra + " >>\nstream\n" + s + "\nendstream";
    };
    objects[3] = "<< /Type /Font /Subtype /Type0 /BaseFont /Inter /Encoding /Identity-H "
                 "/DescendantFonts [4 0 R] /ToUnicode 7 0 R >>";
    objects[4] = "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Inter /CIDSystemInfo << "
                 "/Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor 5 0 R "
                 "/CIDToGIDMap 8 0 R /DW 1000 /W " +
                 widths + " >>";
    objects[5] =
        "<< /Type /FontDescriptor /FontName /Inter /Flags 32 /FontBBox [-1000 -1000 3000 3000] "
        "/ItalicAngle 0 /Ascent 1000 /Descent -250 /CapHeight 730 /StemV 80 /FontFile2 6 0 R >>";
    objects[6] = stream(std::string(reinterpret_cast<const char *>(font.b.data()), font.b.size()),
                        " /Length1 " + std::to_string(font.b.size()));
    objects.push_back(stream(map));
    objects.push_back(stream(gids));
    std::string kids;
    for (size_t i = 0; i < pages.size(); i++) {
      int id = static_cast<int>(objects.size());
      kids += std::to_string(id) + " 0 R ";
      objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] /Resources << "
                        "/Font << /F1 3 0 R >> >> /Contents " +
                        std::to_string(id + 1) + " 0 R >>");
      objects.push_back(stream(pages[i]));
    }
    objects[2] =
        "<< /Type /Pages /Count " + std::to_string(pages.size()) + " /Kids [" + kids + "] >>";
    std::string pdf = "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
    std::vector<size_t> offsets(objects.size());
    for (size_t i = 1; i < objects.size(); i++) {
      offsets[i] = pdf.size();
      pdf += std::to_string(i) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    auto pos = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size()) + "\n0000000000 65535 f \n";
    for (size_t i = 1; i < objects.size(); i++) {
      std::ostringstream o;
      o << std::setfill('0') << std::setw(10) << offsets[i] << " 00000 n \n";
      pdf += o.str();
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size()) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(pos) + "\n%%EOF\n";
    writeText(path, pdf);
  }
};
} // namespace
void writePdf(const std::filesystem::path &path, const Snapshot &s, const std::string &group,
              const std::string &wave, const std::string &comment) {
  auto wi = std::find_if(s.waves.begin(), s.waves.end(), [&](auto &w) { return w.id == wave; });
  if (wi == s.waves.end() || classmates(s, group).empty())
    throw std::runtime_error("Выберите класс и обследование");
  auto sum = summarize(s, group, wave);
  Pdf p;
  p.line("Школьный мониторинг · " + group, 21);
  p.line(wi->title + " · " + wi->date);
  p.line("Сохранено анкет: " + std::to_string(sum.answered) + " / " + std::to_string(sum.total));
  if (s.demo)
    p.line("СИНТЕТИЧЕСКИЕ ДЕМОДАННЫЕ");
  p.line(disclaimer, 9);
  p.title("Настроение на уроках");
  std::vector<double> widths = {177, 82, 82, 82, 82};
  auto moodHead = [&] {
    p.tableRow({"Предмет", "Красный", "Жёлтый", "Зелёный", "Ответов"}, widths, true);
  };
  moodHead();
  for (size_t i = 0; i < subjects.size(); i++) {
    if (p.y < 75) {
      p.fresh();
      moodHead();
    }
    auto m = sum.mood[i];
    auto cell = [&](int n) {
      return std::to_string(n) + " (" +
             (m.total() ? std::to_string(static_cast<int>(std::lround(100.0 * n / m.total())))
                        : "—") +
             "%)";
    };
    p.tableRow({subjects[i], cell(m.red), cell(m.yellow), cell(m.green), std::to_string(m.total())},
               widths);
  }
  p.title("Лесенка");
  p.line("Ступени 1–" + std::to_string(s.steps) + ", снизу вверх. Число ступеней предварительное.",
         9);
  widths = {255, 125, 125};
  p.tableRow({"Шкала", "Среднее", "Ответов"}, widths, true);
  for (size_t i = 0; i < scales.size(); i++) {
    auto l = sum.ladder[i];
    if (p.y < 75) {
      p.fresh();
      p.tableRow({"Шкала", "Среднее", "Ответов"}, widths, true);
    }
    p.tableRow({scales[i], l.n ? number(l.mean()) : "—", std::to_string(l.n)}, widths);
  }
  p.title("Входящие выборы одноклассников");
  auto peers = classmates(s, group);
  auto keys = choiceKeys(peers[0].grade);
  widths = {177};
  for (size_t k = 0; k < keys.size(); k++)
    widths.push_back(328.0 / keys.size());
  std::vector<std::string> header = {"Ученик"};
  for (auto &k : keys) {
    auto label = k;
    if (k.find("День рождения") == 0)
      label = "ДР" + k.substr(k.size() - 1);
    if (k.find("Олимпиада") == 0)
      label = "Олимп." + k.substr(k.size() - 1);
    header.push_back(label);
  }
  p.line("Плюс — выбор, минус — исключение по вопросу. Это не рейтинг личности.", 9);
  p.tableRow(header, widths, true);
  std::vector<Matrix> matrices;
  for (auto &k : keys)
    matrices.push_back(matrix(s, group, wave, k));
  for (auto &peer : peers) {
    if (p.y < 75) {
      p.fresh();
      p.tableRow(header, widths, true);
    }
    std::vector<std::string> row = {peer.name};
    for (auto &m : matrices)
      row.push_back(std::to_string(m.incoming[peer.id]));
    p.tableRow(row, widths);
  }
  p.title("Заключение специалиста");
  p.line(comment.empty() ? "Не заполнено. Интерпретацию выполняет специалист." : comment);
  p.line("Сформировано " + today() + ". При сравнении учитывайте состав класса и число ответов.",
         9);
  p.save(path);
}
} // namespace monitor
