#include "model.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <wx/bmpbuttn.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/fileconf.h>
#include <wx/grid.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/snglinst.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>
#include <wx/wx.h>
using namespace monitor;
namespace {
const wxColour Blue("#005bff"), Ink("#001a34"), Muted("#708398"), Paper("#f2f5f9"), Line("#e2e8f0"),
    Pink("#f1117e");
wxString U(const std::string &s) {
  return wxString::FromUTF8(s.data(), s.size());
}
std::string S(const wxString &s) {
  return s.ToStdString(wxConvUTF8);
}
std::filesystem::path P(const wxString &s) {
  return std::filesystem::u8path(S(s));
}
wxString W(const std::filesystem::path &p) {
  return U(p.u8string());
}
wxFont Font(int size, bool bold = false) {
  wxFont f(wxFontInfo(size).FaceName("Inter"));
  f.SetWeight(bold ? wxFONTWEIGHT_SEMIBOLD : wxFONTWEIGHT_NORMAL);
  return f;
}
wxStaticText *Label(wxWindow *parent, const wxString &text, int size = 10, bool bold = false) {
  auto p = new wxStaticText(parent, wxID_ANY, text);
  p->SetForegroundColour(Ink);
  p->SetFont(Font(size, bold));
  return p;
}
// Bitmap states keep native button keyboard, dialog and accessibility semantics,
// while drawing the same Inter typography and surfaces on macOS and Windows.
class StyledButton : public wxBitmapButton {
  wxString text_;
  bool primary_;
  wxBitmap Surface(wxSize size, bool hover = false, bool pressed = false, bool focus = false,
                   bool disabled = false) {
    wxBitmap bitmap;
    bitmap.CreateWithLogicalSize(size, GetDPIScaleFactor());
    wxMemoryDC memory(bitmap);
    auto background = GetParent()->GetBackgroundColour();
    memory.SetBackground(wxBrush(background.IsOk() ? background : *wxWHITE));
    memory.Clear();
    {
      wxGCDC dc(memory);
      wxRect r(2, 2, size.x - 4, size.y - 5);
      wxColour fill = primary_ ? Blue : *wxWHITE;
      if (hover)
        fill = primary_ ? wxColour("#1468ff") : wxColour("#f2f7ff");
      if (pressed) {
        fill = primary_ ? wxColour("#004bd6") : wxColour("#e5efff");
        r.Offset(0, 1);
      }
      if (disabled)
        fill = wxColour("#e2e8f0");
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(wxColour("#dce5f2")));
      dc.DrawRoundedRectangle(2, 4, size.x - 4, size.y - 5, 7);
      if (focus) {
        dc.SetPen(wxPen(wxColour("#99bdff"), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, 8);
      }
      dc.SetPen(wxPen(primary_ ? fill : Line));
      dc.SetBrush(wxBrush(fill));
      dc.DrawRoundedRectangle(r, 7);
      dc.SetFont(Font(10, true));
      dc.SetTextForeground(disabled ? Muted : (primary_ ? *wxWHITE : Ink));
      auto z = dc.GetTextExtent(text_);
      dc.DrawText(text_, (size.x - z.x) / 2, (size.y - z.y) / 2 + (pressed ? 1 : 0));
    }
    memory.SelectObject(wxNullBitmap);
    return bitmap;
  }
  void UpdateSurfaces() {
    auto size = GetClientSize();
    if (size.x < 8 || size.y < 8)
      return;
    SetBitmap(Surface(size));
    SetBitmapCurrent(Surface(size, true));
    SetBitmapPressed(Surface(size, false, true));
    SetBitmapFocus(Surface(size, false, false, true));
    SetBitmapDisabled(Surface(size, false, false, false, true));
  }

public:
  StyledButton(wxWindow *parent, const wxString &text, bool primary, int id)
      : wxBitmapButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
        text_(text), primary_(primary) {
    SetName(text);
    SetLabel(text);
    SetFont(Font(10, true));
    SetCursor(wxCursor(wxCURSOR_HAND));
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    auto z = dc.GetTextExtent(text);
    auto size = wxSize(z.x + 36, 42);
    SetBitmapMargins(0, 0);
    SetMinSize(size);
    SetSize(size);
    UpdateSurfaces();
    Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      UpdateSurfaces();
      e.Skip();
    });
  }
};
StyledButton *Button(wxWindow *parent, const wxString &text, bool primary = false,
                     int id = wxID_ANY) {
  return new StyledButton(parent, text, primary, id);
}
void DrawSurface(wxDC &dc, wxSize size) {
  dc.SetBackground(wxBrush(Paper));
  dc.Clear();
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(wxColour("#e8edf5")));
  dc.DrawRoundedRectangle(1, 3, size.x - 2, size.y - 3, 10);
  dc.SetPen(wxPen(Line));
  dc.SetBrush(*wxWHITE_BRUSH);
  dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 4, 10);
}
class Card : public wxPanel {
public:
  explicit Card(wxWindow *p) : wxPanel(p) {
    SetBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
      wxAutoBufferedPaintDC buffer(this);
      wxGCDC dc(buffer);
      DrawSurface(dc, GetClientSize());
    });
  }
};
wxSizer *DialogButtons(wxDialog *d) {
  auto row = new wxBoxSizer(wxHORIZONTAL);
  auto cancel = Button(d, "Отмена", false, wxID_CANCEL);
  auto save = Button(d, "Сохранить", true, wxID_OK);
  row->Add(cancel, 0, wxRIGHT, 12);
  row->AddStretchSpacer();
  row->Add(save);
  return row;
}
void Error(wxWindow *parent, const std::exception &e) {
  wxMessageBox(U(e.what()), "Проверьте данные", wxOK | wxICON_WARNING, parent);
}
class Nav : public wxControl {
  bool active_ = false, hover_ = false;
  std::function<void()> action_;

public:
  Nav(wxWindow *p, const wxString &label, std::function<void()> f)
      : wxControl(p, wxID_ANY, wxDefaultPosition, wxSize(208, 44), wxBORDER_NONE),
        action_(std::move(f)) {
    SetLabel(label);
    SetName(label);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Font(12, true));
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
      hover_ = true;
      Refresh();
      e.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
      hover_ = false;
      Refresh();
      e.Skip();
    });
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
      wxAutoBufferedPaintDC dc(this);
      dc.SetBackground(wxBrush(Blue));
      dc.Clear();
      if (active_ || hover_ || HasFocus()) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(active_ ? "#2673ff" : "#1a6bff")));
        dc.DrawRoundedRectangle(GetClientRect(), 8);
      }
      dc.SetTextForeground(*wxWHITE);
      dc.SetFont(GetFont());
      auto sz = dc.GetTextExtent(GetLabel());
      dc.DrawText(GetLabel(), 14, (GetClientSize().y - sz.y) / 2);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
      SetFocus();
      action_();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &e) {
      if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_SPACE)
        action_();
      else
        e.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
      Refresh();
      e.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) {
      Refresh();
      e.Skip();
    });
  }
  void Active(bool v) {
    active_ = v;
    Refresh();
  }
};
class Chart : public wxPanel {
  Summary summary_;
  bool mood_;
  int steps_;

public:
  Chart(wxWindow *p, Summary s, bool mood, int steps)
      : wxPanel(p), summary_(s), mood_(mood), steps_(steps) {
    SetMinSize(wxSize(200, 390));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &Chart::Paint, this);
  }
  void Paint(wxPaintEvent &) {
    wxAutoBufferedPaintDC buffer(this);
    wxGCDC dc(buffer);
    DrawSurface(dc, GetClientSize());
    int w = GetClientSize().x;
    dc.SetTextForeground(Ink);
    dc.SetFont(Font(13, true));
    dc.DrawText(mood_ ? "Настроение на уроках" : "Самооценка · лесенка", 18, 18);
    dc.SetFont(Font(9));
    if (mood_) {
      int legendX = 18;
      for (auto pair :
           std::vector<std::pair<wxString, wxColour>>{{"Красный", wxColour("#f34d70")},
                                                      {"Жёлтый", wxColour("#ffb52e")},
                                                      {"Зелёный", wxColour("#00be6c")}}) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(pair.second));
        dc.DrawCircle(legendX + 4, 60, 4);
        dc.SetTextForeground(Muted);
        dc.DrawText(pair.first, legendX + 13, 53);
        legendX += 90;
      }
      for (size_t i = 0; i < subjects.size(); i++) {
        int y = 88 + static_cast<int>(i) * 29;
        dc.SetTextForeground(Ink);
        wxString name = U(subjects[i]);
        while (dc.GetTextExtent(name).x > 128 && name.size() > 1)
          name.RemoveLast();
        dc.DrawText(name, 18, y);
        int bx = 154, bw = std::max(30, w - bx - 45);
        auto m = summary_.mood[i];
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(Paper));
        dc.DrawRoundedRectangle(bx, y + 3, bw, 12, 3);
        int counts[] = {m.red, m.yellow, m.green};
        wxColour colors[] = {wxColour("#f34d70"), wxColour("#ffb52e"), wxColour("#00be6c")};
        double pos = bx;
        for (int k = 0; k < 3; k++)
          if (m.total()) {
            double next = pos + bw * counts[k] / static_cast<double>(m.total());
            dc.SetBrush(wxBrush(colors[k]));
            dc.DrawRectangle(wxRect(static_cast<int>(pos), y + 3,
                                    static_cast<int>(next) - static_cast<int>(pos), 12));
            pos = next;
          }
        dc.SetTextForeground(Muted);
        dc.DrawText(wxString::Format("%d", m.total()), w - 32, y);
      }
      dc.DrawText("Пропуски исключены из долей", 18, 361);
    } else {
      for (size_t i = 0; i < scales.size(); i++) {
        int y = 64 + static_cast<int>(i) * 55;
        auto l = summary_.ladder[i];
        dc.SetTextForeground(Ink);
        dc.DrawText(U(scales[i]), 18, y);
        wxString val =
            l.n ? wxString::Format("%.2f / %d · n=%d", l.mean(), steps_, l.n) : "Нет ответов";
        dc.SetTextForeground(Muted);
        dc.DrawText(val, std::max(18, w - dc.GetTextExtent(val).x - 18), y + 18);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour("#e5efff")));
        dc.DrawRoundedRectangle(18, y + 38, w - 36, 6, 3);
        if (l.n) {
          dc.SetBrush(wxBrush(Blue));
          dc.DrawRoundedRectangle(18, y + 38, static_cast<int>((w - 36) * l.mean() / steps_), 6, 3);
        }
      }
      dc.SetTextForeground(Muted);
      dc.DrawText("Шкала предварительная, без нормативов", 18, 361);
    }
  }
};
wxChoice *Choice(wxWindow *p, const wxArrayString &values) {
  auto c = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, values);
  c->SetFont(Font(10));
  c->SetMinSize(wxSize(130, 34));
  if (!values.empty())
    c->SetSelection(0);
  return c;
}
void GridStyle(wxGrid *g) {
  g->EnableEditing(false);
  g->SetDefaultCellFont(Font(10));
  g->SetLabelFont(Font(9, true));
  g->SetLabelBackgroundColour(wxColour("#e5efff"));
  g->SetLabelTextColour(Ink);
  g->SetGridLineColour(Line);
  g->SetDefaultRowSize(31);
  g->SetDefaultCellTextColour(Ink);
  g->SetSelectionMode(wxGrid::wxGridSelectRows);
}
void EditAnswer(wxWindow *parent, Store &db, const Student &pupil, const std::string &wave) {
  auto state = db.snapshot();
  Json payload = Json::object();
  for (auto &a : state.answers)
    if (a.student == pupil.id && a.wave == wave)
      payload = a.payload;
  auto peers = classmates(state, pupil.group);
  wxDialog d(parent, wxID_ANY, U(pupil.name + " · " + pupil.group), wxDefaultPosition,
             wxSize(810, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  d.SetBackgroundColour(*wxWHITE);
  d.SetFont(Font(10));
  auto root = new wxBoxSizer(wxVERTICAL);
  auto scroll = new wxScrolledWindow(&d);
  scroll->SetScrollRate(0, 12);
  auto form = new wxBoxSizer(wxVERTICAL);
  auto title = [&](const wxString &text) {
    form->Add(Label(scroll, text, 13, true), 0, wxTOP | wxBOTTOM, 14);
  };
  title("01 / Настроение");
  auto hint = Label(scroll,
                    "Красный — чаще плохое. Жёлтый — хорошее, но не люблю отвечать. Зелёный — "
                    "отличное, люблю отвечать.",
                    9);
  hint->Wrap(690);
  form->Add(hint, 0, wxBOTTOM, 12);
  auto moodGrid = new wxFlexGridSizer(2, 10, 18);
  moodGrid->AddGrowableCol(1, 1);
  std::vector<wxChoice *> mood, ladder;
  std::array<std::string, 4> moodKeys = {"", "red", "yellow", "green"};
  for (auto &subject : subjects) {
    moodGrid->Add(
        Label(scroll,
              U(subject == "Школа в целом" && pupil.grade == 1 ? "Понедельник в школе" : subject)),
        0, wxALIGN_CENTER_VERTICAL);
    auto c = Choice(scroll, {"— Пропуск", "Красный", "Жёлтый", "Зелёный"});
    auto value = payload.value("mood", Json::object()).value(subject, std::string{});
    for (int i = 0; i < 4; i++)
      if (value == moodKeys[i])
        c->SetSelection(i);
    mood.push_back(c);
    moodGrid->Add(c, 1, wxEXPAND);
  }
  form->Add(moodGrid, 0, wxEXPAND);
  title(U("02 / Лесенка · 1 снизу, " + std::to_string(state.steps) + " сверху"));
  auto ladderGrid = new wxFlexGridSizer(2, 10, 18);
  ladderGrid->AddGrowableCol(1, 1);
  for (auto &scale : scales) {
    ladderGrid->Add(Label(scroll, U(scale)), 0, wxALIGN_CENTER_VERTICAL);
    wxArrayString options;
    options.Add("— Пропуск");
    for (int i = 1; i <= state.steps; i++)
      options.Add(wxString::Format("%d", i));
    auto c = Choice(scroll, options);
    int value = payload.value("ladder", Json::object()).value(scale, 0);
    if (value >= 0 && value <= state.steps)
      c->SetSelection(value);
    ladder.push_back(c);
    ladderGrid->Add(c, 1, wxEXPAND);
  }
  form->Add(ladderGrid, 0, wxEXPAND);
  title("03 / Выборы одноклассников");
  auto keys = choiceKeys(pupil.grade);
  wxArrayString names;
  names.Add("— Не указан");
  std::vector<std::string> ids = {""};
  for (auto &p : peers)
    if (p.id != pupil.id) {
      names.Add(U(p.name));
      ids.push_back(p.id);
    }
  auto choicesGrid = new wxFlexGridSizer(3, 10, 12);
  choicesGrid->AddGrowableCol(1, 1);
  choicesGrid->AddGrowableCol(2, 1);
  std::vector<std::array<wxChoice *, 2>> choices;
  for (auto &key : keys) {
    auto label = key == "Кино-" ? std::string("Не хватило билета в кино")
                                : key.substr(0, key.size() - 1) +
                                      (key.back() == '+' ? " · выбрали" : " · не выбрали");
    choicesGrid->Add(Label(scroll, U(label)), 0, wxALIGN_CENTER_VERTICAL);
    std::array<wxChoice *, 2> pair;
    auto values = payload.value("choices", Json::object()).value(key, Json::array());
    for (int j = 0; j < 2; j++) {
      pair[j] = Choice(scroll, names);
      pair[j]->SetMinSize(wxSize(170, 34));
      if (static_cast<size_t>(j) < values.size() && values[j].is_string()) {
        auto found = std::find(ids.begin(), ids.end(), values[j].get<std::string>());
        if (found != ids.end())
          pair[j]->SetSelection(static_cast<int>(found - ids.begin()));
      }
      if (key == "Кино-" && j == 1)
        pair[j]->Disable();
      choicesGrid->Add(pair[j], 1, wxEXPAND);
    }
    choices.push_back(pair);
  }
  form->Add(choicesGrid, 0, wxEXPAND);
  title("Примечание сотрудника");
  auto note = new wxTextCtrl(scroll, wxID_ANY, U(payload.value("note", std::string{})),
                             wxDefaultPosition, wxSize(-1, 85), wxTE_MULTILINE);
  note->SetMaxLength(2000);
  form->Add(note, 0, wxEXPAND | wxBOTTOM, 16);
  auto padded = new wxBoxSizer(wxVERTICAL);
  padded->Add(form, 1, wxEXPAND | wxALL, 22);
  scroll->SetSizer(padded);
  root->Add(scroll, 1, wxEXPAND);
  auto bottom = new wxBoxSizer(wxHORIZONTAL);
  bottom->Add(Label(&d, "Пустые поля считаются пропусками", 9), 1, wxALIGN_CENTER_VERTICAL);
  auto cancel = Button(&d, "Отмена", false, wxID_CANCEL);
  auto save = Button(&d, "Сохранить ответы", true);
  bottom->Add(cancel, 0, wxRIGHT, 10);
  bottom->Add(save);
  root->Add(bottom, 0, wxEXPAND | wxALL, 18);
  d.SetSizer(root);
  save->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
    try {
      Json p = {{"mood", Json::object()},
                {"ladder", Json::object()},
                {"choices", Json::object()},
                {"note", S(note->GetValue())}};
      for (size_t i = 0; i < mood.size(); i++)
        if (mood[i]->GetSelection() > 0)
          p["mood"][subjects[i]] = moodKeys[mood[i]->GetSelection()];
      for (size_t i = 0; i < ladder.size(); i++)
        if (ladder[i]->GetSelection() > 0)
          p["ladder"][scales[i]] = ladder[i]->GetSelection();
      for (size_t i = 0; i < choices.size(); i++) {
        p["choices"][keys[i]] = Json::array();
        for (int j = 0; j < (keys[i] == "Кино-" ? 1 : 2); j++) {
          int selected = choices[i][j]->GetSelection();
          if (selected > 0)
            p["choices"][keys[i]].push_back(ids.at(selected));
        }
      }
      db.save(pupil.id, wave, p);
      d.EndModal(wxID_OK);
    } catch (const std::exception &e) {
      Error(&d, e);
    }
  });
  auto area = wxGetClientDisplayRect();
  d.SetSize(std::min(850, area.width - 40), std::min(740, area.height - 50));
  d.CentreOnParent();
  d.CallAfter([scroll, first = mood.front()] {
    first->SetFocus();
    scroll->Scroll(0, 0);
  });
  d.ShowModal();
}
class MainFrame : public wxFrame {
  std::unique_ptr<Store> db_;
  Snapshot state_;
  std::filesystem::path dir_;
  std::string group_, wave_, criterion_ = "Поход+", comment_;
  int tab_ = 0;
  wxChoice *classChoice_, *waveChoice_;
  wxSimplebook *book_;
  wxStaticText *heading_;
  std::vector<std::string> groups_;
  std::vector<Nav *> nav_;
  wxFileConfig config_;
  template <class F> void Guard(F f) {
    try {
      f();
    } catch (const std::exception &e) {
      Error(this, e);
    }
  }
  wxScrolledWindow *Page() {
    auto p = new wxScrolledWindow(book_);
    p->SetBackgroundColour(Paper);
    p->SetScrollRate(0, 12);
    return p;
  }
  void AddPage(wxWindow *p, const wxString &label) {
    book_->AddPage(p, label);
  }
  wxBoxSizer *Body(wxWindow *p) {
    auto s = new wxBoxSizer(wxVERTICAL);
    p->SetSizer(s);
    return s;
  }
  void Text(wxWindow *p, wxSizer *s, const wxString &text, int size = 10, bool bold = false,
            int gap = 12) {
    auto t = Label(p, text, size, bold);
    t->Wrap(820);
    s->Add(t, 0, wxEXPAND | wxBOTTOM, gap);
  }
  void Remember() {
    config_.Write("database", W(db_->path()));
    config_.Flush();
  }
  void Switch(int n) {
    tab_ = n;
    book_->SetSelection(n);
    static const wxString titles[] = {"Обзор класса", "Ученики и анкеты", "Матрица выборов",
                                      "Динамика обследований", "База и правила"};
    heading_->SetLabel(titles[n]);
    for (size_t i = 0; i < nav_.size(); i++)
      nav_[i]->Active(static_cast<int>(i) == n);
    Layout();
  }
  void Render() {
    state_ = db_->snapshot();
    std::set<std::string> gs;
    for (auto &p : state_.students)
      gs.insert(p.group);
    groups_.assign(gs.begin(), gs.end());
    if (!gs.count(group_))
      group_ = groups_.empty() ? "" : groups_[0];
    if (std::none_of(state_.waves.begin(), state_.waves.end(),
                     [&](auto &w) { return w.id == wave_; }))
      wave_ = state_.waves.empty() ? "" : state_.waves[0].id;
    classChoice_->Clear();
    for (auto &g : groups_)
      classChoice_->Append(U(g));
    if (groups_.empty())
      classChoice_->Append("Нет классов");
    auto gi = std::find(groups_.begin(), groups_.end(), group_);
    classChoice_->SetSelection(gi == groups_.end() ? 0 : static_cast<int>(gi - groups_.begin()));
    waveChoice_->Clear();
    for (size_t i = 0; i < state_.waves.size(); i++) {
      auto &w = state_.waves[i];
      waveChoice_->Append(U(w.title + " · " + w.date));
      if (w.id == wave_)
        waveChoice_->SetSelection(static_cast<int>(i));
    }
    if (state_.waves.empty()) {
      waveChoice_->Append("Нет обследований");
      waveChoice_->SetSelection(0);
    }
    Freeze();
    book_->DeleteAllPages();
    Overview();
    Students();
    Social();
    History();
    Settings();
    Switch(tab_);
    SetStatusText(U((state_.demo ? "Демо · вымышленные данные   |   " : "Рабочая база   |   ") +
                    db_->path().u8string()));
    Thaw();
  }
  bool Ready() {
    if (group_.empty() || wave_.empty()) {
      wxMessageBox("Добавьте ученика и обследование.", "Начало работы", wxOK | wxICON_INFORMATION,
                   this);
      return false;
    }
    return true;
  }
  void Overview() {
    auto p = Page();
    auto s = Body(p);
    if (group_.empty() || wave_.empty()) {
      Text(p, s, "Подготовьте первое обследование", 20, true);
      Text(p, s,
           "Добавьте ученика с классом и создайте обследование. Ответы сохраняются в выбранной "
           "локальной базе.");
      AddPage(p, "Обзор");
      return;
    }
    auto sum = summarize(state_, group_, wave_);
    auto cards = new wxBoxSizer(wxHORIZONTAL);
    std::vector<std::pair<wxString, wxString>> metrics = {
        {"Учеников в классе", wxString::Format("%d", sum.total)},
        {"Сохранено анкет", wxString::Format("%d / %d", sum.answered, sum.total)},
        {"Обследований в базе", wxString::Format("%zu", state_.waves.size())}};
    for (auto &m : metrics) {
      auto card = new Card(p);
      card->SetBackgroundColour(*wxWHITE);
      auto box = new wxBoxSizer(wxVERTICAL);
      auto caption = Label(card, m.first, 10);
      caption->SetForegroundColour(Muted);
      box->Add(caption, 0, wxLEFT | wxRIGHT | wxTOP, 20);
      box->Add(Label(card, m.second, 29, true), 0, wxALL, 18);
      card->SetSizer(box);
      cards->Add(card, 1, wxRIGHT, 12);
    }
    s->Add(cards, 0, wxEXPAND | wxBOTTOM, 20);
    auto charts = new wxBoxSizer(wxHORIZONTAL);
    charts->Add(new Chart(p, sum, true, state_.steps), 3, wxEXPAND | wxRIGHT, 16);
    charts->Add(new Chart(p, sum, false, state_.steps), 2, wxEXPAND);
    s->Add(charts, 0, wxEXPAND | wxBOTTOM, 18);
    Text(p, s, U(disclaimer), 9);
    Text(p, s, "Справка по обследованию", 14, true);
    auto comment =
        new wxTextCtrl(p, wxID_ANY, U(comment_), wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE);
    comment->SetHint("Комментарий специалиста для PDF (не сохраняется в базе)");
    comment->Bind(wxEVT_TEXT,
                  [this, comment](wxCommandEvent &) { comment_ = S(comment->GetValue()); });
    s->Add(comment, 0, wxEXPAND | wxBOTTOM, 12);
    auto actions = new wxBoxSizer(wxHORIZONTAL);
    auto c = Button(p, "Экспорт CSV");
    auto pdf = Button(p, "Сохранить PDF", true);
    actions->Add(c, 0, wxRIGHT, 10);
    actions->Add(pdf);
    s->Add(actions, 0, wxBOTTOM, 18);
    c->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Export(false); });
    pdf->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Export(true); });
    AddPage(p, "Обзор");
  }
  void Students() {
    auto p = Page();
    auto s = Body(p);
    Text(p, s, U("Ученики · " + group_), 16, true);
    auto search = new wxTextCtrl(p, wxID_ANY);
    search->SetHint("Поиск по имени или коду");
    s->Add(search, 0, wxEXPAND | wxBOTTOM, 14);
    auto list = new wxListCtrl(p, wxID_ANY, wxDefaultPosition, wxSize(-1, 400),
                               wxLC_REPORT | wxLC_SINGLE_SEL);
    list->InsertColumn(0, "Ученик", wxLIST_FORMAT_LEFT, 300);
    list->InsertColumn(1, "Класс", wxLIST_FORMAT_LEFT, 90);
    list->InsertColumn(2, "Анкета", wxLIST_FORMAT_LEFT, 130);
    list->InsertColumn(3, "Примечание", wxLIST_FORMAT_LEFT, 250);
    auto peers = classmates(state_, group_);
    auto fill = [this, list, peers](const wxString &needle) {
      list->DeleteAllItems();
      for (size_t i = 0; i < peers.size(); i++) {
        auto &peer = peers[i];
        if (!needle.empty() && U(peer.name).Lower().Find(needle.Lower()) == wxNOT_FOUND)
          continue;
        const Answer *ans = nullptr;
        for (auto &a : state_.answers)
          if (a.student == peer.id && a.wave == wave_)
            ans = &a;
        long row = list->InsertItem(list->GetItemCount(), U(peer.name));
        list->SetItem(row, 1, U(peer.group));
        list->SetItem(row, 2, ans ? "Сохранена" : "Нет ответов");
        if (ans)
          list->SetItem(row, 3, U(ans->payload.value("note", std::string{})));
        list->SetItemData(row, static_cast<long>(i));
      }
      if (list->GetItemCount())
        list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    };
    fill("");
    search->Bind(wxEVT_TEXT, [fill, search](wxCommandEvent &) { fill(search->GetValue()); });
    s->Add(list, 1, wxEXPAND | wxBOTTOM, 12);
    auto open = Button(p, "Открыть анкету", true);
    s->Add(open, 0, wxBOTTOM, 12);
    auto edit = [this, list, peers] {
      if (!Ready())
        return;
      long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
      if (row < 0)
        return;
      auto pupil = peers.at(list->GetItemData(row));
      Guard([&] {
        EditAnswer(this, *db_, pupil, wave_);
        Render();
      });
    };
    open->Bind(wxEVT_BUTTON, [edit](wxCommandEvent &) { edit(); });
    list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [edit](wxListEvent &) { edit(); });
    Text(p, s, "«Сохранена» означает наличие записи, а не полную заполненность анкеты.", 9);
    AddPage(p, "Ученики");
  }
  void Social() {
    auto p = Page();
    auto s = Body(p);
    auto peers = classmates(state_, group_);
    if (peers.empty() || wave_.empty()) {
      Text(p, s, "Добавьте учеников и обследование.");
      AddPage(p, "Матрица");
      return;
    }
    auto keys = choiceKeys(peers[0].grade);
    if (std::find(keys.begin(), keys.end(), criterion_) == keys.end())
      criterion_ = keys[0];
    wxArrayString options;
    int selected = 0;
    for (size_t i = 0; i < keys.size(); i++) {
      options.Add(U(keys[i]));
      if (keys[i] == criterion_)
        selected = static_cast<int>(i);
    }
    auto choose = Choice(p, options);
    choose->SetSelection(selected);
    choose->Bind(wxEVT_CHOICE, [this, choose, keys](wxCommandEvent &) {
      criterion_ = keys.at(choose->GetSelection());
      CallAfter([this] { Render(); });
    });
    s->Add(choose, 0, wxBOTTOM, 14);
    auto m = matrix(state_, group_, wave_, criterion_);
    Text(p, s, U("Взаимных пар по этому вопросу: " + std::to_string(m.mutual)), 14, true);
    Text(p, s,
         "Строка — кто выбирает, столбец — кого выбирает. Номер столбца соответствует номеру "
         "ученика в строках. «−» — исключение по формулировке вопроса.",
         9);
    auto grid = new wxGrid(p, wxID_ANY);
    grid->CreateGrid(static_cast<int>(peers.size()), static_cast<int>(peers.size() + 2));
    GridStyle(grid);
    grid->SetRowLabelSize(205);
    for (size_t i = 0; i < peers.size(); i++) {
      grid->SetRowLabelValue(static_cast<int>(i),
                             wxString::Format("%02zu · ", i + 1) + U(peers[i].name));
      grid->SetColLabelValue(static_cast<int>(i), wxString::Format("%02zu", i + 1));
      grid->SetColSize(static_cast<int>(i), 38);
      for (size_t j = 0; j < peers.size(); j++) {
        bool hit = m.links.count({peers[i].id, peers[j].id});
        grid->SetCellValue(static_cast<int>(i), static_cast<int>(j),
                           i == j ? "·"
                           : hit  ? (criterion_.back() == '-' ? "−" : "+")
                                  : "");
        grid->SetCellAlignment(static_cast<int>(i), static_cast<int>(j), wxALIGN_CENTER,
                               wxALIGN_CENTER);
        if (i == j)
          grid->SetCellBackgroundColour(static_cast<int>(i), static_cast<int>(j), Paper);
        else if (hit)
          grid->SetCellBackgroundColour(static_cast<int>(i), static_cast<int>(j),
                                        criterion_.back() == '-' ? wxColour("#fff0f6")
                                                                 : wxColour("#e5efff"));
      }
      grid->SetCellValue(static_cast<int>(i), static_cast<int>(peers.size()),
                         wxString::Format("%d", m.incoming[peers[i].id]));
      grid->SetCellValue(static_cast<int>(i), static_cast<int>(peers.size() + 1),
                         wxString::Format("%d", m.outgoing[peers[i].id]));
    }
    grid->SetColLabelValue(static_cast<int>(peers.size()), "Входящие");
    grid->SetColLabelValue(static_cast<int>(peers.size() + 1), "Исходящие");
    s->Add(grid, 1, wxEXPAND | wxBOTTOM, 16);
    Text(p, s,
         "Расчёты ведутся отдельно по вопросам. Общий рейтинг и категории личности не "
         "присваиваются.",
         9);
    AddPage(p, "Матрица");
  }
  void History() {
    auto p = Page();
    auto s = Body(p);
    Text(p, s, U("Сравнение по времени · " + group_), 16, true);
    Text(p, s,
         "Каждый период считается отдельно. Сравнивайте число ответивших и состав класса. Это не "
         "парный статистический анализ одних и тех же детей.",
         9);
    if (state_.waves.empty()) {
      AddPage(p, "Динамика");
      return;
    }
    auto grid = new wxGrid(p, wxID_ANY);
    grid->CreateGrid(static_cast<int>(state_.waves.size()), 7);
    GridStyle(grid);
    grid->SetRowLabelSize(0);
    grid->SetColLabelValue(0, "Обследование");
    grid->SetColSize(0, 300);
    grid->SetColLabelValue(1, "Анкет");
    for (int i = 0; i < 5; i++) {
      grid->SetColLabelValue(i + 2, U(scales[i]));
      grid->SetColSize(i + 2, 125);
    }
    for (size_t i = 0; i < state_.waves.size(); i++) {
      auto &wave = state_.waves[i];
      auto sum = summarize(state_, group_, wave.id);
      grid->SetCellValue(static_cast<int>(i), 0, U(wave.title + " · " + wave.date));
      grid->SetCellValue(static_cast<int>(i), 1,
                         wxString::Format("%d / %d", sum.answered, sum.total));
      for (int j = 0; j < 5; j++) {
        auto a = sum.ladder[j];
        grid->SetCellValue(static_cast<int>(i), j + 2,
                           a.n ? wxString::Format("%.2f · n=%d", a.mean(), a.n) : "—");
      }
    }
    grid->SetMinSize(wxSize(0, std::min(300, static_cast<int>(state_.waves.size() + 1) * 31 + 24)));
    s->Add(grid, 0, wxEXPAND | wxBOTTOM, 20);
    for (auto &wave : state_.waves) {
      Text(p, s, U(wave.title + " · " + wave.date), 12, true);
      s->Add(new Chart(p, summarize(state_, group_, wave.id), true, state_.steps), 0,
             wxEXPAND | wxBOTTOM, 20);
    }
    AddPage(p, "Динамика");
  }
  void Settings() {
    auto p = Page();
    auto s = Body(p);
    Text(p, s, "Файл базы данных", 16, true);
    Text(p, s, W(db_->path()));
    Text(p, s,
         state_.demo
             ? "Сейчас открыта отдельная демонстрационная база. Все имена и ответы вымышленные."
             : "Рабочая база. Данные хранятся локально и не передаются в интернет.");
    auto actions = new wxBoxSizer(wxHORIZONTAL);
    auto create = Button(p, "Новая пустая база", true), open = Button(p, "Открыть базу / копию"),
         backup = Button(p, "Резервная копия");
    actions->Add(create, 0, wxRIGHT, 10);
    actions->Add(open, 0, wxRIGHT, 10);
    actions->Add(backup);
    s->Add(actions, 0, wxBOTTOM, 20);
    create->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { NewDatabase(); });
    open->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OpenDatabase(); });
    backup->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Backup(); });
    Text(p, s, "Правила обработки", 16, true);
    Text(p, s,
         "Настроение: число и доля каждого цвета по предмету; пропуски исключаются из "
         "знаменателя.\nЛесенка: исходная ступень, среднее и число ответов. 1 — низ, N — "
         "верх.\nСоциометрия: отдельные матрицы, входящие/исходящие выборы и взаимные "
         "пары.\nДинамика: описательные сводки каждого периода, без вывода о достоверности "
         "изменений.");
    Text(p, s, U(disclaimer));
    auto line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(Label(p, "Число ступеней (только до ввода анкет)"), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);
    wxArrayString nums;
    for (int i = 3; i <= 10; i++)
      nums.Add(wxString::Format("%d", i));
    auto choice = Choice(p, nums);
    choice->SetSelection(state_.steps - 3);
    choice->Enable(state_.answers.empty());
    line->Add(choice, 0, wxRIGHT, 10);
    auto save = Button(p, "Сохранить шкалу");
    save->Enable(state_.answers.empty());
    line->Add(save);
    save->Bind(wxEVT_BUTTON, [this, choice](wxCommandEvent &) {
      Guard([&] {
        db_->setSteps(choice->GetSelection() + 3);
        Render();
      });
    });
    s->Add(line, 0, wxBOTTOM, 22);
    Text(p, s,
         "Совместимы SQLite-базы Electron-версии 0.1–0.2. Перед открытием закройте другое "
         "приложение, работающее с тем же файлом. Состав класса для исторических сводок берётся "
         "текущий. База и экспорты не шифруются приложением.",
         9);
    Text(p, s, "C++17 · wxWidgets 3.3.4 · SQLite 3.54.1 · один сотрудник · без облака", 9);
    AddPage(p, "Настройки");
  }
  void AddStudent() {
    wxDialog d(this, wxID_ANY, "Добавить ученика");
    d.SetBackgroundColour(*wxWHITE);
    d.SetFont(Font(10));
    auto s = new wxBoxSizer(wxVERTICAL);
    s->Add(Label(&d, "Имя или код ученика"), 0, wxBOTTOM, 7);
    auto name = new wxTextCtrl(&d, wxID_ANY);
    s->Add(name, 0, wxEXPAND | wxBOTTOM, 16);
    s->Add(Label(&d, "Класс"), 0, wxBOTTOM, 7);
    auto group = new wxTextCtrl(&d, wxID_ANY, U(group_));
    s->Add(group, 0, wxEXPAND | wxBOTTOM, 16);
    s->Add(Label(&d, "Параллель"), 0, wxBOTTOM, 7);
    auto grade = Choice(&d, {"1 класс", "2 класс", "3 класс", "4 класс"});
    auto peers = classmates(state_, group_);
    if (!peers.empty())
      grade->SetSelection(peers[0].grade - 1);
    s->Add(grade, 0, wxEXPAND | wxBOTTOM, 18);
    s->Add(DialogButtons(&d), 0, wxEXPAND);
    d.FindWindow(wxID_OK)->SetLabel("Сохранить");
    d.FindWindow(wxID_CANCEL)->SetLabel("Отмена");
    auto outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(s, 1, wxEXPAND | wxALL, 24);
    d.SetSizerAndFit(outer);
    d.SetMinSize(wxSize(420, -1));
    d.SetSize(std::max(420, d.GetSize().x), d.GetSize().y);
    d.CentreOnParent();
    while (d.ShowModal() == wxID_OK) {
      try {
        auto cn = S(group->GetValue());
        db_->addStudent(S(name->GetValue()), cn, grade->GetSelection() + 1);
        group_ = cn;
        Render();
        break;
      } catch (const std::exception &e) {
        Error(&d, e);
      }
    }
  }
  void AddWave() {
    wxDialog d(this, wxID_ANY, "Новое обследование");
    d.SetBackgroundColour(*wxWHITE);
    d.SetFont(Font(10));
    auto s = new wxBoxSizer(wxVERTICAL);
    s->Add(Label(&d, "Название"), 0, wxBOTTOM, 7);
    auto title = new wxTextCtrl(&d, wxID_ANY);
    title->SetHint("Например, начало учебного года");
    s->Add(title, 0, wxEXPAND | wxBOTTOM, 18);
    s->Add(Label(&d, "Дата"), 0, wxBOTTOM, 7);
    auto date = new wxDatePickerCtrl(&d, wxID_ANY, wxDefaultDateTime, wxDefaultPosition,
                                     wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
    s->Add(date, 0, wxEXPAND | wxBOTTOM, 24);
    s->Add(DialogButtons(&d), 0, wxEXPAND);
    d.FindWindow(wxID_OK)->SetLabel("Сохранить");
    d.FindWindow(wxID_CANCEL)->SetLabel("Отмена");
    auto outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(s, 1, wxEXPAND | wxALL, 24);
    d.SetSizerAndFit(outer);
    d.SetMinSize(wxSize(450, -1));
    d.SetSize(std::max(450, d.GetSize().x), d.GetSize().y);
    d.CentreOnParent();
    while (d.ShowModal() == wxID_OK) {
      try {
        auto old = state_.waves;
        db_->addWave(S(title->GetValue()), S(date->GetValue().FormatISODate()));
        for (auto &w : db_->snapshot().waves)
          if (std::none_of(old.begin(), old.end(), [&](auto &x) { return x.id == w.id; }))
            wave_ = w.id;
        Render();
        break;
      } catch (const std::exception &e) {
        Error(&d, e);
      }
    }
  }
  void NewDatabase() {
    wxFileDialog f(this, "Создать пустую рабочую базу", "", "school-monitor.sqlite",
                   "SQLite (*.sqlite)|*.sqlite", wxFD_SAVE);
    if (f.ShowModal() != wxID_OK)
      return;
    Guard([&] {
      auto path = P(f.GetPath());
      if (std::filesystem::exists(path))
        throw std::runtime_error("Выберите новое имя: существующий файл не будет заменён");
      auto next = std::make_unique<Store>(path);
      db_ = std::move(next);
      group_.clear();
      wave_.clear();
      comment_.clear();
      Remember();
      Render();
    });
  }
  void OpenDatabase() {
    wxFileDialog f(this, "Открыть базу / копию", "", "", "SQLite (*.sqlite)|*.sqlite|Все файлы|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (f.ShowModal() != wxID_OK)
      return;
    Guard([&] {
      auto next = std::make_unique<Store>(P(f.GetPath()), false);
      db_ = std::move(next);
      group_.clear();
      wave_.clear();
      comment_.clear();
      Remember();
      Render();
    });
  }
  void Backup() {
    wxFileDialog f(this, "Резервная копия", "", U("school-monitor-backup-" + today() + ".sqlite"),
                   "SQLite (*.sqlite)|*.sqlite", wxFD_SAVE);
    if (f.ShowModal() != wxID_OK)
      return;
    Guard([&] {
      db_->backup(P(f.GetPath()));
      SetStatusText("Резервная копия сохранена");
    });
  }
  void Export(bool pdf) {
    if (!Ready())
      return;
    wxFileDialog f(this, pdf ? "Сохранить PDF" : "Экспорт CSV", "",
                   pdf ? "school-monitor.pdf" : "school-monitor.csv",
                   pdf ? "PDF (*.pdf)|*.pdf" : "CSV (*.csv)|*.csv",
                   wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (f.ShowModal() != wxID_OK)
      return;
    Guard([&] {
      if (pdf)
        writePdf(P(f.GetPath()), state_, group_, wave_, comment_);
      else
        writeText(P(f.GetPath()), csv(state_, group_, wave_));
      SetStatusText(pdf ? "Справка PDF сохранена" : "CSV сохранён");
    });
  }

public:
  MainFrame(std::unique_ptr<Store> db, const std::filesystem::path &dir)
      : wxFrame(nullptr, wxID_ANY, "Школьный мониторинг · C++", wxDefaultPosition,
                wxSize(1240, 850)),
        db_(std::move(db)), dir_(dir),
        config_("SchoolMonitorNative", "", W(dir / "settings.ini"), "", wxCONFIG_USE_LOCAL_FILE) {
    SetMinSize(wxSize(1020, 650));
    SetFont(Font(10));
    auto root = new wxBoxSizer(wxHORIZONTAL);
    auto side = new wxPanel(this);
    side->SetBackgroundColour(Blue);
    side->SetMinSize(wxSize(230, -1));
    auto sidebar = new wxBoxSizer(wxVERTICAL);
    auto brand = new wxBoxSizer(wxHORIZONTAL);
    auto mark = new wxPanel(side, wxID_ANY, wxDefaultPosition, wxSize(44, 44));
    mark->SetMinSize(wxSize(44, 44));
    mark->SetBackgroundStyle(wxBG_STYLE_PAINT);
    mark->Bind(wxEVT_PAINT, [mark](wxPaintEvent &) {
      wxAutoBufferedPaintDC dc(mark);
      dc.SetBackground(wxBrush(Blue));
      dc.Clear();
      dc.SetBrush(*wxWHITE_BRUSH);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRoundedRectangle(mark->GetClientRect(), 12);
      dc.SetTextForeground(Blue);
      dc.SetFont(Font(23, true));
      auto z = dc.GetTextExtent("М");
      dc.DrawText("М", (44 - z.x) / 2, (44 - z.y) / 2);
    });
    brand->Add(mark, 0, wxRIGHT, 12);
    auto name = Label(side, "Школьный\nмониторинг", 12, true);
    name->SetForegroundColour(*wxWHITE);
    brand->Add(name, 1, wxALIGN_CENTER_VERTICAL);
    sidebar->Add(brand, 0, wxEXPAND | wxALL, 18);
    sidebar->AddSpacer(30);
    for (auto &label : wxArrayString{"Обзор класса", "Ученики и анкеты", "Матрица выборов",
                                     "Динамика", "База и правила"}) {
      int i = static_cast<int>(nav_.size());
      auto n = new Nav(side, label, [this, i] { Switch(i); });
      nav_.push_back(n);
      sidebar->Add(n, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    }
    sidebar->AddStretchSpacer();
    auto local = Label(side, "Локальная база\nОдин сотрудник · C++", 9);
    local->SetForegroundColour(*wxWHITE);
    sidebar->Add(local, 0, wxALL, 22);
    side->SetSizer(sidebar);
    root->Add(side, 0, wxEXPAND);
    auto main = new wxPanel(this);
    main->SetBackgroundColour(Paper);
    auto content = new wxBoxSizer(wxVERTICAL);
    auto top = new wxBoxSizer(wxHORIZONTAL);
    heading_ = Label(main, "Обзор класса", 22, true);
    top->Add(heading_, 1, wxALIGN_CENTER_VERTICAL);
    auto backup = Button(main, "Резервная копия");
    top->Add(backup);
    backup->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Backup(); });
    content->Add(top, 0, wxEXPAND | wxBOTTOM, 24);
    auto filters = new wxBoxSizer(wxHORIZONTAL);
    auto field = [&](const wxString &text, wxChoice *&c, int stretch) {
      auto v = new wxBoxSizer(wxVERTICAL);
      v->Add(Label(main, text, 9, true), 0, wxBOTTOM, 7);
      c = Choice(main, {});
      v->Add(c, 0, wxEXPAND);
      filters->Add(v, stretch, wxEXPAND | wxRIGHT, 12);
    };
    field("Класс", classChoice_, 0);
    field("Обследование", waveChoice_, 1);
    auto wave = Button(main, "+ Обследование");
    auto student = Button(main, "+ Ученик", true);
    filters->Add(wave, 0, wxALIGN_BOTTOM | wxRIGHT, 10);
    filters->Add(student, 0, wxALIGN_BOTTOM);
    wave->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { AddWave(); });
    student->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { AddStudent(); });
    classChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
      if (!groups_.empty())
        group_ = groups_.at(classChoice_->GetSelection());
      comment_.clear();
      CallAfter([this] { Render(); });
    });
    waveChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
      if (!state_.waves.empty())
        wave_ = state_.waves.at(waveChoice_->GetSelection()).id;
      comment_.clear();
      CallAfter([this] { Render(); });
    });
    content->Add(filters, 0, wxEXPAND | wxBOTTOM, 20);
    book_ = new wxSimplebook(main);
    book_->SetMinSize(wxSize(0, 0));
    content->Add(book_, 1, wxEXPAND);
    auto padded = new wxBoxSizer(wxVERTICAL);
    padded->Add(content, 1, wxEXPAND | wxALL, 26);
    main->SetSizer(padded);
    root->Add(main, 1, wxEXPAND);
    SetSizer(root);
    CreateStatusBar();
    Render();
    Centre();
  }
  void Smoke(const std::filesystem::path &dir) {
    auto state = db_->snapshot();
    if (state.students.size() != 48 || state.answers.size() != 88)
      throw std::runtime_error("Smoke demo mismatch");
    for (int i = 0; i < 5; i++)
      Switch(i);
    Switch(0);
    writePdf(dir / "smoke.pdf", state, "1А", "w2", "Нативная проверка C++ / wxWidgets");
    writeText(dir / "smoke.csv", csv(state, "1А", "w2"));
    db_->backup(dir / "backup.sqlite");
    writeText(dir / "smoke-ok.txt",
              "Native window initialized; five pages; SQLite; PDF; CSV; backup: OK\n");
  }
};
class App : public wxApp {
  std::unique_ptr<wxSingleInstanceChecker> checker_;
  std::unique_ptr<wxTimer> timer_;

public:
  bool OnInit() override {
    SetExitOnFrameDelete(true);
    SetAppName("SchoolMonitorNative");
    SetAppDisplayName("Школьный мониторинг");
    bool smoke = argc == 3 && wxString(argv[1]) == "--smoke-test";
    auto dir = smoke ? P(wxString(argv[2])) : P(wxStandardPaths::Get().GetUserDataDir());
    try {
      std::filesystem::create_directories(dir);
      if (!smoke) {
        checker_ =
            std::make_unique<wxSingleInstanceChecker>("SchoolMonitorNative-" + wxGetUserId());
        if (checker_->IsAnotherRunning()) {
          wxMessageBox("Приложение уже запущено.", "Школьный мониторинг");
          return false;
        }
      }
#ifdef __WXOSX__
      auto font = P(wxStandardPaths::Get().GetResourcesDir()) / "Fonts" / "Inter.ttf";
#else
      auto font = dir / "Inter.ttf";
      if (!std::filesystem::exists(font)) {
        auto &b = fontBytes();
        writeText(font, std::string(reinterpret_cast<const char *>(b.data()), b.size()));
      }
#endif
      if (!wxFont::AddPrivateFont(W(font)))
        throw std::runtime_error("Не удалось загрузить встроенный шрифт Inter");
      wxFileConfig cfg("SchoolMonitorNative", "", W(dir / "settings.ini"), "",
                       wxCONFIG_USE_LOCAL_FILE);
      wxString last;
      cfg.Read("database", &last);
      auto dbPath =
          (!last.empty() && std::filesystem::exists(P(last))) ? P(last) : dir / "demo.sqlite";
      bool fresh = !std::filesystem::exists(dbPath);
      auto db = std::make_unique<Store>(dbPath);
      if (fresh)
        db->seedDemo();
      auto frame = new MainFrame(std::move(db), dir);
      SetTopWindow(frame);
      frame->Show();
      if (smoke) {
        timer_ = std::make_unique<wxTimer>(this);
        Bind(wxEVT_TIMER, [this, frame, dir](wxTimerEvent &) {
          try {
            frame->Smoke(dir);
          } catch (const std::exception &e) {
            writeText(dir / "smoke-error.txt", e.what());
          }
          frame->Close(true);
          ExitMainLoop();
        });
        timer_->StartOnce(1500);
      }
      return true;
    } catch (const std::exception &e) {
      if (smoke)
        writeText(dir / "smoke-error.txt", e.what());
      else
        Error(nullptr, e);
      return false;
    }
  }
};
} // namespace
wxIMPLEMENT_APP(App);
