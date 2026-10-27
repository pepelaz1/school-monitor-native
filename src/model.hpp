#pragma once
#include "json.hpp"
#include "sqlite3.h"
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
using Json = nlohmann::json;
namespace monitor {
extern const std::array<std::string, 9> subjects;
extern const std::array<std::string, 5> scales;
extern const std::array<std::string, 3> criteria;
extern const std::string disclaimer;
struct Student {
  std::string id, name, group;
  int grade;
};
struct Wave {
  std::string id, title, date;
};
struct Answer {
  std::string wave, student;
  Json payload;
};
struct Snapshot {
  std::vector<Student> students;
  std::vector<Wave> waves;
  std::vector<Answer> answers;
  int steps = 5;
  bool demo = false;
};
struct Mood {
  int red = 0, yellow = 0, green = 0;
  int total() const {
    return red + yellow + green;
  }
};
struct Scale {
  int n = 0;
  double sum = 0;
  double mean() const {
    return n ? sum / n : 0;
  }
};
struct Summary {
  int total = 0, answered = 0;
  std::array<Mood, 9> mood{};
  std::array<Scale, 5> ladder{};
};
struct Matrix {
  std::set<std::pair<std::string, std::string>> links;
  std::map<std::string, int> incoming, outgoing;
  int mutual = 0;
};
std::vector<Student> classmates(const Snapshot &, const std::string &);
std::vector<std::string> choiceKeys(int grade);
Json validate(const Json &, const Student &, const std::vector<Student> &, int steps);
Summary summarize(const Snapshot &, const std::string &group, const std::string &wave);
Matrix matrix(const Snapshot &, const std::string &group, const std::string &wave,
              const std::string &key);
std::string csv(const Snapshot &, const std::string &group, const std::string &wave);
std::string csvCell(std::string);
std::string today();
std::string uid();
bool validDate(const std::string &);
void writeText(const std::filesystem::path &, const std::string &);
const std::vector<unsigned char> &fontBytes();
void writePdf(const std::filesystem::path &, const Snapshot &, const std::string &group,
              const std::string &wave, const std::string &comment);
class Store {
  sqlite3 *db_ = nullptr;
  std::filesystem::path path_;
  void exec(const std::string &);
  void run(const std::string &, const std::vector<std::string> &);
  std::vector<Json> query(const std::string &, const std::vector<std::string> &params = {}) const;

public:
  explicit Store(const std::filesystem::path &, bool create = true);
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  const std::filesystem::path &path() const {
    return path_;
  }
  Snapshot snapshot() const;
  void seedDemo();
  void addStudent(std::string name, std::string group, int grade);
  void addWave(std::string title, std::string date);
  void save(const std::string &student, const std::string &wave, const Json &payload);
  void setSteps(int);
  void backup(const std::filesystem::path &) const;
};
} // namespace monitor
