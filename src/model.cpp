#include "model.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace monitor {
const std::array<std::string, 9> subjects = {"Математика",      "Русский язык",   "Чтение",
                                             "Английский язык", "Окружающий мир", "Информатика",
                                             "Музыка",          "Физкультура",    "Школа в целом"};
const std::array<std::string, 5> scales = {"Умные", "Добрые", "Красивые", "Со мной дружат",
                                           "Хорошо учатся"};
const std::array<std::string, 3> criteria = {"Поход", "Олимпиада", "День рождения"};
const std::string disclaimer = "Описательная обработка. Ключи методик предварительные. Пороги и "
                               "диагнозы не применяются; заключение пишет специалист.";
static std::string trim(std::string s) {
  auto ws = [](unsigned char c) { return c <= 32; };
  while (!s.empty() && ws(s.front()))
    s.erase(s.begin());
  while (!s.empty() && ws(s.back()))
    s.pop_back();
  return s;
}
std::string uid() {
  static std::mt19937_64 rng(std::random_device{}());
  std::ostringstream o;
  o << std::hex << rng() << rng();
  return o.str();
}
std::string today() {
  auto t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream o;
  o << std::put_time(&tm, "%Y-%m-%d");
  return o.str();
}
bool validDate(const std::string &s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-')
    return false;
  for (int i = 0; i < 10; i++)
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
      return false;
  int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
  if (y < 1900 || m < 1 || m > 12)
    return false;
  int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
    days[1] = 29;
  return d >= 1 && d <= days[m - 1];
}
void writeText(const std::filesystem::path &p, const std::string &s) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f || !f.write(s.data(), static_cast<std::streamsize>(s.size())))
    throw std::runtime_error("Не удалось записать файл");
}
std::vector<Student> classmates(const Snapshot &s, const std::string &group) {
  std::vector<Student> r;
  for (auto &p : s.students)
    if (p.group == group)
      r.push_back(p);
  return r;
}
std::vector<std::string> choiceKeys(int grade) {
  std::vector<std::string> k;
  for (auto &c : criteria) {
    k.push_back(c + "+");
    if (grade > 1)
      k.push_back(c + "-");
  }
  if (grade == 1)
    k.push_back("Кино-");
  return k;
}
Json validate(const Json &p, const Student &student, const std::vector<Student> &peers, int steps) {
  if (!p.is_object())
    throw std::runtime_error("Некорректная анкета");
  Json result = {{"mood", Json::object()},
                 {"ladder", Json::object()},
                 {"choices", Json::object()},
                 {"note", ""}};
  auto object = [&](const std::string &k) {
    if (p.contains(k) && !p[k].is_object())
      throw std::runtime_error("Некорректный блок анкеты");
    return p.value(k, Json::object());
  };
  auto mood = object("mood"), ladder = object("ladder"), choices = object("choices");
  for (auto &x : subjects)
    if (mood.contains(x) && !mood[x].is_null() && mood[x] != "") {
      if (mood[x] != "red" && mood[x] != "yellow" && mood[x] != "green")
        throw std::runtime_error("Неизвестный цвет");
      result["mood"][x] = mood[x];
    }
  for (auto &x : scales)
    if (ladder.contains(x) && !ladder[x].is_null() && ladder[x] != "") {
      if (!ladder[x].is_number_integer() || ladder[x].get<double>() < 1 ||
          ladder[x].get<double>() > steps)
        throw std::runtime_error("Ступень вне диапазона");
      result["ladder"][x] = ladder[x];
    }
  std::set<std::string> allowed;
  for (auto &peer : peers)
    if (peer.group == student.group && peer.id != student.id)
      allowed.insert(peer.id);
  for (auto &k : choiceKeys(student.grade)) {
    Json a = choices.value(k, Json::array());
    if (!a.is_array() || a.size() > (k == "Кино-" ? 1u : 2u))
      throw std::runtime_error("Слишком много выборов");
    std::set<std::string> seen;
    for (auto &x : a)
      if (!x.is_string() || !allowed.count(x.get<std::string>()) ||
          !seen.insert(x.get<std::string>()).second)
        throw std::runtime_error("Выберите разных одноклассников, кроме самого ученика");
    result["choices"][k] = a;
  }
  for (auto &c : criteria)
    if (student.grade > 1)
      for (auto &x : result["choices"][c + "+"])
        for (auto &y : result["choices"][c + "-"])
          if (x == y)
            throw std::runtime_error("Один ученик не может быть выбран и исключён в одном вопросе");
  if (p.contains("note") && !p["note"].is_string())
    throw std::runtime_error("Некорректное примечание");
  auto note = trim(p.value("note", std::string{}));
  size_t chars = 0;
  for (unsigned char c : note)
    if ((c & 0xc0) != 0x80)
      ++chars;
  if (chars > 2000)
    throw std::runtime_error("Примечание длиннее 2000 символов");
  result["note"] = note;
  return result;
}
Summary summarize(const Snapshot &s, const std::string &group, const std::string &wave) {
  Summary r;
  std::set<std::string> ids;
  for (auto &p : classmates(s, group))
    ids.insert(p.id);
  r.total = static_cast<int>(ids.size());
  for (auto &a : s.answers)
    if (a.wave == wave && ids.count(a.student)) {
      r.answered++;
      auto mood = a.payload.value("mood", Json::object()),
           ladder = a.payload.value("ladder", Json::object());
      for (size_t i = 0; i < subjects.size(); i++)
        if (mood.contains(subjects[i])) {
          auto v = mood[subjects[i]];
          if (v == "red")
            r.mood[i].red++;
          if (v == "yellow")
            r.mood[i].yellow++;
          if (v == "green")
            r.mood[i].green++;
        }
      for (size_t i = 0; i < scales.size(); i++)
        if (ladder.contains(scales[i]) && ladder[scales[i]].is_number()) {
          r.ladder[i].sum += ladder[scales[i]].get<double>();
          r.ladder[i].n++;
        }
    }
  return r;
}
Matrix matrix(const Snapshot &s, const std::string &group, const std::string &wave,
              const std::string &key) {
  Matrix r;
  std::set<std::string> ids;
  for (auto &p : classmates(s, group)) {
    ids.insert(p.id);
    r.incoming[p.id] = 0;
    r.outgoing[p.id] = 0;
  }
  for (auto &a : s.answers)
    if (a.wave == wave && ids.count(a.student)) {
      auto ch = a.payload.value("choices", Json::object());
      for (auto &x : ch.value(key, Json::array()))
        if (x.is_string() && ids.count(x.get<std::string>()) && x != a.student)
          r.links.insert({a.student, x.get<std::string>()});
    }
  for (auto &e : r.links) {
    r.outgoing[e.first]++;
    r.incoming[e.second]++;
    if (e.first < e.second && r.links.count({e.second, e.first}))
      r.mutual++;
  }
  return r;
}
std::string csvCell(std::string s) {
  auto first = s.find_first_not_of(' ');
  if (first != std::string::npos && std::string("=+-@\t\r\n").find(s[first]) != std::string::npos)
    s = "'" + s;
  std::string r = "\"";
  for (char c : s) {
    if (c == '"')
      r += '"';
    r += c;
  }
  return r + '"';
}
std::string csv(const Snapshot &s, const std::string &group, const std::string &wave) {
  std::string out = "\xef\xbb\xbf";
  auto row = [&](const std::vector<std::string> &values) {
    for (size_t i = 0; i < values.size(); i++) {
      if (i)
        out += ';';
      out += csvCell(values[i]);
    }
    out += "\r\n";
  };
  std::vector<std::string> head = {"Ученик", "Класс", "Обследование", "Дата"};
  head.insert(head.end(), subjects.begin(), subjects.end());
  head.insert(head.end(), scales.begin(), scales.end());
  head.push_back("Примечание");
  row(head);
  auto wi = std::find_if(s.waves.begin(), s.waves.end(), [&](auto &w) { return w.id == wave; });
  if (wi == s.waves.end())
    throw std::runtime_error("Обследование не найдено");
  for (auto &p : classmates(s, group)) {
    Json a = {{"mood", Json::object()}, {"ladder", Json::object()}};
    for (auto &x : s.answers)
      if (x.student == p.id && x.wave == wave)
        a = x.payload;
    std::vector<std::string> v = {p.name, p.group, wi->title, wi->date};
    for (auto &x : subjects)
      v.push_back(a["mood"].value(x, std::string{}));
    for (auto &x : scales)
      v.push_back(a["ladder"].contains(x) ? std::to_string(a["ladder"][x].get<int>()) : "");
    v.push_back(a.value("note", std::string{}));
    row(v);
  }
  return out;
}
Store::Store(const std::filesystem::path &p, bool create) : path_(std::filesystem::absolute(p)) {
  if (!create && !std::filesystem::exists(path_))
    throw std::runtime_error("База не найдена");
  if (create)
    std::filesystem::create_directories(path_.parent_path());
  auto u = path_.u8string();
  if (sqlite3_open_v2(u.c_str(), &db_, SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0),
                      nullptr) != SQLITE_OK) {
    std::string e = db_ ? sqlite3_errmsg(db_) : "SQLite";
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(e);
  }
  try {
    if (!create) {
      auto tables = query("SELECT name FROM sqlite_master WHERE type='table' AND name IN "
                          "('meta','students','waves','answers')");
      if (tables.size() != 4)
        throw std::runtime_error("Файл не является совместимой базой школьного мониторинга");
    }
    sqlite3_busy_timeout(db_, 3000);
    exec("PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS meta(key "
         "TEXT PRIMARY KEY,value TEXT NOT NULL); CREATE TABLE IF NOT EXISTS students(id TEXT "
         "PRIMARY KEY,name TEXT NOT NULL,class_name TEXT NOT NULL,grade INTEGER NOT NULL "
         "CHECK(grade BETWEEN 1 AND 4)); CREATE TABLE IF NOT EXISTS waves(id TEXT PRIMARY "
         "KEY,title TEXT NOT NULL,date TEXT NOT NULL); CREATE TABLE IF NOT EXISTS answers(wave_id "
         "TEXT NOT NULL REFERENCES waves(id),student_id TEXT NOT NULL REFERENCES "
         "students(id),payload TEXT NOT NULL,PRIMARY KEY(wave_id,student_id)); INSERT OR IGNORE "
         "INTO meta VALUES('steps','5'),('demo','0');");
    snapshot();
#ifndef _WIN32
    chmod(path_.c_str(), 0600);
#endif
  } catch (...) {
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }
}
Store::~Store() {
  if (db_)
    sqlite3_close(db_);
}
void Store::exec(const std::string &sql) {
  char *err = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "SQLite";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}
void Store::run(const std::string &sql, const std::vector<std::string> &values) {
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db_));
  for (size_t i = 0; i < values.size(); i++)
    sqlite3_bind_text(st, static_cast<int>(i + 1), values[i].c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  std::string error = sqlite3_errmsg(db_);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
    throw std::runtime_error(error);
}
std::vector<Json> Store::query(const std::string &sql,
                               const std::vector<std::string> &values) const {
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db_));
  for (size_t i = 0; i < values.size(); i++)
    sqlite3_bind_text(st, static_cast<int>(i + 1), values[i].c_str(), -1, SQLITE_TRANSIENT);
  std::vector<Json> rows;
  int rc;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    Json row = Json::object();
    for (int c = 0; c < sqlite3_column_count(st); c++) {
      auto n = sqlite3_column_name(st, c);
      if (sqlite3_column_type(st, c) == SQLITE_INTEGER)
        row[n] = sqlite3_column_int(st, c);
      else {
        auto p = sqlite3_column_text(st, c);
        row[n] = p ? reinterpret_cast<const char *>(p) : "";
      }
    }
    rows.push_back(row);
  }
  std::string error = sqlite3_errmsg(db_);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
    throw std::runtime_error(error);
  return rows;
}
Snapshot Store::snapshot() const {
  Snapshot s;
  for (auto &j : query("SELECT * FROM students ORDER BY class_name,name"))
    s.students.push_back({j["id"], j["name"], j["class_name"], j["grade"]});
  for (auto &j : query("SELECT * FROM waves ORDER BY date DESC,id"))
    s.waves.push_back({j["id"], j["title"], j["date"]});
  for (auto &j : query("SELECT * FROM answers"))
    s.answers.push_back(
        {j["wave_id"], j["student_id"], Json::parse(j["payload"].get<std::string>())});
  s.steps = std::stoi(
      query("SELECT value FROM meta WHERE key='steps'").at(0)["value"].get<std::string>());
  if (s.steps < 3 || s.steps > 10)
    throw std::runtime_error("Некорректное число ступеней");
  s.demo = query("SELECT value FROM meta WHERE key='demo'").at(0)["value"] == "1";
  return s;
}
void Store::addStudent(std::string name, std::string group, int grade) {
  name = trim(name);
  group = trim(group);
  if (name.empty() || name.size() > 400 || group.empty() || group.size() > 120 || grade < 1 ||
      grade > 4)
    throw std::runtime_error("Проверьте имя, класс и параллель");
  for (auto &p : snapshot().students)
    if (p.group == group) {
      if (p.grade != grade)
        throw std::runtime_error("У одного класса должна быть одна параллель");
      if (p.name == name)
        throw std::runtime_error("Ученик уже есть в классе");
    }
  run("INSERT INTO students VALUES(?,?,?,?)", {uid(), name, group, std::to_string(grade)});
}
void Store::addWave(std::string title, std::string date) {
  title = trim(title);
  if (title.empty() || title.size() > 400 || !validDate(date))
    throw std::runtime_error("Проверьте название и дату");
  run("INSERT INTO waves VALUES(?,?,?)", {uid(), title, date});
}
void Store::save(const std::string &student, const std::string &wave, const Json &payload) {
  auto s = snapshot();
  auto it =
      std::find_if(s.students.begin(), s.students.end(), [&](auto &p) { return p.id == student; });
  if (it == s.students.end() ||
      std::none_of(s.waves.begin(), s.waves.end(), [&](auto &w) { return w.id == wave; }))
    throw std::runtime_error("Ученик или обследование не найдены");
  auto p = validate(payload, *it, classmates(s, it->group), s.steps);
  run("INSERT INTO answers VALUES(?,?,?) ON CONFLICT(wave_id,student_id) DO UPDATE SET "
      "payload=excluded.payload",
      {wave, student, p.dump()});
}
void Store::setSteps(int n) {
  if (n < 3 || n > 10 || !snapshot().answers.empty())
    throw std::runtime_error("Шкала 3–10 меняется только в базе без ответов");
  run("UPDATE meta SET value=? WHERE key='steps'", {std::to_string(n)});
}
void Store::backup(const std::filesystem::path &p) const {
  if (std::filesystem::exists(p))
    throw std::runtime_error("Выберите новое имя файла копии");
  sqlite3 *dest = nullptr;
  if (sqlite3_open(p.u8string().c_str(), &dest) != SQLITE_OK) {
    if (dest)
      sqlite3_close(dest);
    throw std::runtime_error("Не удалось открыть копию");
  }
  auto b = sqlite3_backup_init(dest, "main", db_, "main");
  int rc = b ? sqlite3_backup_step(b, -1) : SQLITE_ERROR;
  if (b)
    sqlite3_backup_finish(b);
  sqlite3_close(dest);
  if (rc != SQLITE_DONE)
    throw std::runtime_error("Не удалось создать копию");
#ifndef _WIN32
  chmod(p.c_str(), 0600);
#endif
}
void Store::seedDemo() {
  auto s = snapshot();
  if (!s.students.empty() || !s.waves.empty())
    return;
  exec("BEGIN IMMEDIATE");
  try {
    run("UPDATE meta SET value='1' WHERE key='demo'", {});
    run("INSERT INTO waves VALUES(?,?,?)", {"w1", "Начало года · демо", "2026-09-10"});
    run("INSERT INTO waves VALUES(?,?,?)", {"w2", "Повторный замер · демо", today()});
    for (int cl = 0; cl < 4; cl++) {
      int grade = cl < 2 ? 1 : 2;
      auto group = std::to_string(grade) + (cl % 2 ? "Б" : "А");
      for (int i = 1; i <= 12; i++) {
        auto id = "s" + std::to_string(cl) + "-" + std::to_string(i);
        auto name = "Демо ученик " + std::string(i < 10 ? "0" : "") + std::to_string(i);
        run("INSERT INTO students VALUES(?,?,?,?)", {id, name, group, std::to_string(grade)});
        for (int w = 1; w <= 2; w++) {
          if (w == 2 && i > 10)
            continue;
          Json p = {{"mood", Json::object()},
                    {"ladder", Json::object()},
                    {"choices", Json::object()},
                    {"note", ""}};
          for (size_t n = 0; n < subjects.size(); n++)
            p["mood"][subjects[n]] =
                std::array<std::string, 3>{"red", "yellow", "green"}[(i + n + w) % 3];
          for (size_t n = 0; n < scales.size(); n++)
            p["ladder"][scales[n]] = 1 + (i + n + w) % 5;
          auto peer = [&](int delta) {
            return "s" + std::to_string(cl) + "-" + std::to_string(((i + delta) % 12) + 1);
          };
          for (auto &c : criteria) {
            p["choices"][c + "+"] = {peer(0), peer(1)};
            if (grade > 1)
              p["choices"][c + "-"] = {peer(5)};
          }
          if (grade == 1)
            p["choices"]["Кино-"] = {peer(5)};
          run("INSERT INTO answers VALUES(?,?,?)", {"w" + std::to_string(w), id, p.dump()});
        }
      }
    }
    exec("COMMIT");
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
}
} // namespace monitor
