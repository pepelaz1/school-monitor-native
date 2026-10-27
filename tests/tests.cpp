#include "model.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace monitor;
static void check(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
template <class F> void rejects(F f) {
  bool threw = false;
  try {
    f();
  } catch (const std::exception &) {
    threw = true;
  }
  check(threw, "expected rejection");
}
int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--verify-db") {
    try {
      Store imported(std::filesystem::u8path(argv[2]), false);
      auto snapshot = imported.snapshot();
      for (auto &a : snapshot.answers) {
        auto it = std::find_if(snapshot.students.begin(), snapshot.students.end(),
                               [&](auto &p) { return p.id == a.student; });
        if (it == snapshot.students.end())
          throw std::runtime_error("orphan student");
        validate(a.payload, *it, classmates(snapshot, it->group), snapshot.steps);
      }
      std::cout << "Compatible SQLite: " << snapshot.students.size() << " students, "
                << snapshot.waves.size() << " waves, " << snapshot.answers.size() << " answers\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << e.what() << "\n";
      return 1;
    }
  }

  auto dir =
      std::filesystem::temp_directory_path() / std::filesystem::u8path("monitor-test-" + uid());
  std::filesystem::create_directories(dir);
  try {
    auto path = dir / "demo.sqlite";
    Store store(path);
    store.seedDemo();
    auto s = store.snapshot();
    check(s.students.size() == 48 && s.answers.size() == 88 && s.waves.size() == 2 && s.demo,
          "demo sizes");
    auto sum = summarize(s, "1А", "w2");
    check(sum.total == 12 && sum.answered == 10 && sum.mood[0].total() == 10, "counts isolation");
    check(sum.ladder[0].n == 10 && sum.ladder[0].mean() == 3, "means");
    auto peers = classmates(s, "1А");
    auto student = peers[0];
    Json blank = validate(Json::object(), student, peers, 5);
    check(blank["mood"].empty() && blank["ladder"].empty(), "missing values");
    Json bad = {{"choices", {{"Поход+", {student.id}}}}};
    rejects([&] { validate(bad, student, peers, 5); });
    bad["choices"]["Поход+"] = {peers[1].id, peers[1].id};
    rejects([&] { validate(bad, student, peers, 5); });
    bad["choices"]["Поход+"] = {"s2-1"};
    rejects([&] { validate(bad, student, peers, 5); });
    rejects([&] { validate(Json{{"ladder", {{scales[0], 6}}}}, student, peers, 5); });
    rejects([&] { validate(Json{{"note", std::string(2001, 'a')}}, student, peers, 5); });
    rejects([&] { validate(Json{{"ladder", {{scales[0], 4294967297LL}}}}, student, peers, 5); });
    {
      auto other = dir / "unrelated.sqlite";
      sqlite3 *raw = nullptr;
      check(sqlite3_open(other.u8string().c_str(), &raw) == SQLITE_OK, "fixture db");
      sqlite3_exec(raw, "CREATE TABLE unrelated(value TEXT)", nullptr, nullptr, nullptr);
      sqlite3_close(raw);
      rejects([&] { Store wrong(other, false); });
      sqlite3_open(other.u8string().c_str(), &raw);
      sqlite3_stmt *stmt = nullptr;
      sqlite3_prepare_v2(raw, "SELECT count(*) FROM sqlite_master WHERE name='students'", -1, &stmt,
                         nullptr);
      check(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0,
            "unrelated DB not changed");
      sqlite3_finalize(stmt);
      sqlite3_close(raw);
    }
    Snapshot two;
    two.students = {{"a", "А", "2Т", 2}, {"b", "Б", "2Т", 2}};
    two.answers = {{"w", "a", {{"choices", {{"Поход+", {"b"}}}}}},
                   {"w", "b", {{"choices", {{"Поход+", {"a"}}}}}}};
    auto m = matrix(two, "2Т", "w", "Поход+");
    check(m.links.size() == 2 && m.mutual == 1 && m.incoming["a"] == 1, "mutual pairs");
    check(matrix(two, "2Т", "w", "Олимпиада+").links.empty(), "criteria isolated");
    rejects([&] {
      validate(Json{{"choices", {{"Поход+", {"b"}}, {"Поход-", {"b"}}}}}, two.students[0],
               two.students, 5);
    });
    check(choiceKeys(1).size() == 4 && choiceKeys(2).size() == 6, "grade questions");
    check(csvCell("=SUM(A1)").find("'=SUM") == 1 && csvCell("a\"b") == "\"a\"\"b\"",
          "csv injection");
    check(validDate("2024-02-29") && !validDate("2026-02-29") && !validDate("2026-13-01"),
          "date validation");
    blank["note"] = "Проверка C++";
    store.save(student.id, "w2", blank);
    store.save(student.id, "w2", blank);
    check(store.snapshot().answers.size() == 88, "unique answer");
    rejects([&] { store.save("missing", "w2", blank); });
    rejects([&] { store.setSteps(7); });
    store.backup(dir / "backup.sqlite");
    {
      Store reopened(dir / "backup.sqlite", false);
      bool found = false;
      for (auto &a : reopened.snapshot().answers)
        if (a.student == student.id && a.wave == "w2")
          found = a.payload["note"] == "Проверка C++";
      check(found, "backup persistence");
    }
    rejects([&] { store.backup(path); });
    {
      Store empty(dir / "empty.sqlite");
      empty.setSteps(7);
      empty.addStudent("Тест", "1Т", 1);
      empty.addWave("Первый", today());
      check(empty.snapshot().students.size() == 1 && empty.snapshot().steps == 7 &&
                !empty.snapshot().demo,
            "empty workflow");
      rejects([&] { empty.addStudent("Другой", "1Т", 2); });
    }
    auto text = csv(store.snapshot(), "1А", "w2");
    check(text.substr(0, 3) == "\xef\xbb\xbf" && text.find("Проверка C++") != std::string::npos,
          "CSV UTF8");
    writePdf(dir / "report.pdf", s, "1А", "w2", "Тестовый отчёт");
    std::ifstream file(dir / "report.pdf", std::ios::binary);
    std::string pdf((std::istreambuf_iterator<char>(file)), {});
    check(pdf.find("%PDF-1.4") == 0 && pdf.find("/FontFile2") != std::string::npos &&
              pdf.find("%%EOF") != std::string::npos,
          "PDF structure and embedded font");
    std::cout << "PASS: demo, missing values, isolation, validation, mutual pairs, dates, SQLite "
                 "backup/reopen, CSV, PDF\n";
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
  std::filesystem::remove_all(dir);
  return 0;
}
