#include "smash_app.h"
#include <wx/filedlg.h>
#include <wx/artprov.h>
#include <algorithm>
#include <set>
#include <map>
#include <tuple>

bool setsLoaded = false;

enum {
    ID_ExportResults = wxID_HIGHEST + 1,
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_ExportResults, MainFrame::OnMenuExport)
    EVT_MENU(wxID_EXIT,        MainFrame::OnMenuExit)
    EVT_MENU(wxID_ABOUT,       MainFrame::OnAbout)
wxEND_EVENT_TABLE()

bool SmashApp::OnInit() {
    MainFrame* frame = new MainFrame("Smash Ultimate Data Analysis Tool");
    frame->Show(true);
    return true;
}

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1200,700)),
      playerHash(), playerTrie(), currentDS(HASH_TABLE), m_loadingDialog(nullptr)
{
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_ExportResults, "&Export Query Results...");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit");

    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT);

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(helpMenu, "&Help");
    SetMenuBar(menuBar);

    dbPath = wxT("ultimate_player_database.db");

    notebook = new wxNotebook(this, wxID_ANY);
    notebook->AddPage(CreateLoadDataPanel(notebook),            "Load Data");
    notebook->AddPage(CreatePlayerStatsPanel(notebook),         "Player Stats");
    notebook->AddPage(CreateHeadToHeadPanel(notebook),          "Head-to-Head");
    notebook->AddPage(CreateCharacterMatchupPanel(notebook),    "Character Matchups");
    notebook->AddPage(CreateStageAnalysisPanel(notebook),       "Stage Analysis");

    CreateStatusBar(2);
    SetStatusText("Welcome to Smash Analyzer!");

    SetStatusText("Loaded DB: " + dbPath, 0);
    UpdateVisitedRowsCounter();

    setsLoaded = false;
}

//----------------- Visited Rows (Backend global version) --------------
void MainFrame::UpdateVisitedRowsCounter() {
    SetStatusText(wxString::Format("Rows Visited: %zu", Backend_GetTotalRowsVisited()), 1);
}

//---------------- BUSY/LOADING HANDLING ----------------
void MainFrame::BusyStart(const wxString& label) {
    if (!m_loadingDialog) {
        m_loadingDialog = new wxDialog(this, wxID_ANY, label, wxDefaultPosition, wxSize(280,80),
                                      wxSTAY_ON_TOP|wxCAPTION|wxDIALOG_NO_PARENT);
        auto* box = new wxBoxSizer(wxVERTICAL);
        box->Add(new wxStaticText(m_loadingDialog, wxID_ANY, label));
        auto* gauge = new wxGauge(m_loadingDialog, wxID_ANY, 100);
        box->Add(gauge, 1, wxEXPAND | wxALL, 10);
        m_loadingDialog->SetSizer(box);
    }
    m_loadingDialog->Raise();
    m_loadingDialog->Show();
    m_loadingDialog->Update();
    wxYield();
}
void MainFrame::BusyEnd() {
    if (m_loadingDialog) {
        m_loadingDialog->Hide();
        delete m_loadingDialog;
        m_loadingDialog = nullptr;
    }
}
void MainFrame::SetEfficiency(wxStaticText* label, double ms) {
    if (ms < 1.0)
        label->SetLabel(wxString::Format("Efficiency: %.2f µs", ms*1000));
    else
        label->SetLabel(wxString::Format("Efficiency: %.2f ms", ms));
}

//------------------ LOAD DATA TAB ----------------------
wxPanel* MainFrame::CreateLoadDataPanel(wxWindow* parent) {
    wxPanel* panel = new wxPanel(parent);
    auto* vbox = new wxBoxSizer(wxVERTICAL);

    auto* desc = new wxStaticText(panel, wxID_ANY,
        "Load the database and benchmark performance of data structures (Hash Table vs Trie):\n"
        "- Build/load time\n- Memory usage (if available)");
    vbox->Add(desc, 0, wxEXPAND | wxALL, 10);

    // Add choice for DS type
    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    m_perfDSChoice = new wxChoice(panel, wxID_ANY);
    m_perfDSChoice->Append("Hash Table");
    m_perfDSChoice->Append("Trie");
    m_perfDSChoice->Append("Both");
    m_perfDSChoice->SetSelection(2);
    hbox->Add(m_perfDSChoice, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 10);
    m_perfRunBtn = new wxButton(panel, wxID_ANY, "Load Database and Benchmark");
    hbox->Add(m_perfRunBtn, 0, wxALIGN_CENTER_VERTICAL);

    vbox->Add(hbox, 0, wxALIGN_LEFT | wxALL, 10);

    m_perfStatusLabel = new wxStaticText(panel, wxID_ANY, "");
    vbox->Add(m_perfStatusLabel, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT, 10);

    m_perfEfficiencyLabel = new wxStaticText(panel, wxID_ANY, "Efficiency: ---");
    vbox->Add(m_perfEfficiencyLabel, 0, wxALIGN_RIGHT | wxRIGHT, 10);

    m_perfResultList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxLC_REPORT | wxLC_SINGLE_SEL);
    m_perfResultList->InsertColumn(0, "Metric", wxLIST_FORMAT_LEFT, 250);
    m_perfResultList->InsertColumn(1, "Hash Table", wxLIST_FORMAT_LEFT, 120);
    m_perfResultList->InsertColumn(2, "Trie", wxLIST_FORMAT_LEFT, 120);
    vbox->Add(m_perfResultList, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 10);

    panel->SetSizer(vbox);

    m_perfDSChoice->Bind(wxEVT_CHOICE, &MainFrame::OnPerfDSChoice, this);
    m_perfRunBtn->Bind(wxEVT_BUTTON, &MainFrame::OnPerfRun, this);
    return panel;
}

void MainFrame::OnPerfDSChoice(wxCommandEvent&) {
    int sel = m_perfDSChoice->GetSelection();
    switch (sel) {
        case 0: currentDS = HASH_TABLE; break;
        case 1: currentDS = TRIE;       break;
        default: currentDS = BOTH;      break;
    }
}

void MainFrame::OnPerfRun(wxCommandEvent&) {
    int sel = m_perfDSChoice->GetSelection();
    BusyStart("Loading database and benchmarking...");
    m_perfStatusLabel->SetLabel("Loading database from file...");
    m_perfResultList->DeleteAllItems();
    wxStopWatch stopwatch;

    if(sel == 0) playerHash.Clear();
    if(sel == 1) playerTrie.Clear();
    if(sel == 2) { playerHash.Clear(); playerTrie.Clear(); }

    bool ok = false;
    size_t record_count = 0;
    double hash_ms = 0, trie_ms = 0;
    size_t hash_mem = 0, trie_mem = 0;

    if (sel == 0) {   // Hash Table only
        stopwatch.Start();
        ok = BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerHash);
        hash_ms = stopwatch.Time();
        record_count = playerHash.GetFirstNRecords(3'000'000).size();
        hash_mem = playerHash.MemoryUsageBytes();
        m_perfResultList->DeleteAllItems();
        m_perfResultList->InsertItem(0, "Build Time (ms)");
        m_perfResultList->SetItem(0, 1, wxString::Format("%.2f", hash_ms));
        m_perfResultList->InsertItem(1, "Memory (KiB)");
        m_perfResultList->SetItem(1, 1, wxString::Format("%zu", hash_mem/1024));
        m_perfStatusLabel->SetLabel(wxString::Format("Loaded %zu player records (Hash Table).", record_count));
    }
    else if (sel == 1) {  // Trie only
        stopwatch.Start();
        ok = BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerTrie);
        trie_ms = stopwatch.Time();
        record_count = playerTrie.GetFirstNRecords(3'000'000).size();
        trie_mem = playerTrie.MemoryUsageBytes();
        m_perfResultList->DeleteAllItems();
        m_perfResultList->InsertItem(0, "Build Time (ms)");
        m_perfResultList->SetItem(0, 2, wxString::Format("%.2f", trie_ms));
        m_perfResultList->InsertItem(1, "Memory (KiB)");
        m_perfResultList->SetItem(1, 2, wxString::Format("%zu", trie_mem/1024));
        m_perfStatusLabel->SetLabel(wxString::Format("Loaded %zu player records (Trie).", record_count));
    }
    else { // BOTH
        ok = BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerHash, playerTrie);
        record_count = playerHash.GetFirstNRecords(3'000'000).size();

        stopwatch.Start();
        playerHash.Clear();
        BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerHash);
        hash_ms = stopwatch.Time();

        stopwatch.Start();
        playerTrie.Clear();
        BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerTrie);
        trie_ms = stopwatch.Time();

        hash_mem = playerHash.MemoryUsageBytes();
        trie_mem = playerTrie.MemoryUsageBytes();

        m_perfResultList->DeleteAllItems();
        m_perfResultList->InsertItem(0, "Build Time (ms)");
        m_perfResultList->SetItem(0, 1, wxString::Format("%.2f", hash_ms));
        m_perfResultList->SetItem(0, 2, wxString::Format("%.2f", trie_ms));

        m_perfResultList->InsertItem(1, "Memory (KiB)");
        m_perfResultList->SetItem(1, 1, wxString::Format("%zu", hash_mem/1024));
        m_perfResultList->SetItem(1, 2, wxString::Format("%zu", trie_mem/1024));
        m_perfStatusLabel->SetLabel(wxString::Format("Loaded %zu player records (Both).", record_count));
    }

    if (!ok || record_count == 0) {
        m_perfStatusLabel->SetLabel("Failed to load database!");
        BusyEnd();
        UpdateVisitedRowsCounter();
        return;
    }

    // Counter auto-updated
    UpdateVisitedRowsCounter();

    double eff = 0.0;
    if (sel == 0) eff = hash_ms;
    else if (sel == 1) eff = trie_ms;
    else eff = std::max(hash_ms, trie_ms);

    SetEfficiency(m_perfEfficiencyLabel, eff);
    BusyEnd();
}


//----------------------- PLAYER STATS TAB ------------------------
wxPanel* MainFrame::CreatePlayerStatsPanel(wxWindow* parent) {
    wxPanel* panel = new wxPanel(parent);
    auto* vbox = new wxBoxSizer(wxVERTICAL);

    auto* topBox = new wxBoxSizer(wxHORIZONTAL);
    m_playerDSChoice = new wxChoice(panel, wxID_ANY);
    m_playerDSChoice->Append("Hash Table"); m_playerDSChoice->Append("Trie");
    m_playerDSChoice->SetSelection(0);

    topBox->Add(new wxStaticText(panel, wxID_ANY, "Data Structure:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    topBox->Add(m_playerDSChoice, 0, wxRIGHT, 15);
    topBox->AddStretchSpacer();
    m_playerEfficiencyLabel = new wxStaticText(panel, wxID_ANY, "Efficiency: ---");
    topBox->Add(m_playerEfficiencyLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);

    vbox->Add(topBox, 0, wxEXPAND | wxALL, 5);

    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "Player Name or ID:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_playerSearchText = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_playerSearchText, 1);
    m_playerSearchBtn = new wxButton(panel, wxID_ANY, "Search");
    hbox->Add(m_playerSearchBtn, 0, wxLEFT, 10);
    m_playerLoadBtn = new wxButton(panel, wxID_ANY, "Load Players");
    hbox->Add(m_playerLoadBtn, 0, wxLEFT, 10);
    m_playerLoadSetsBtn = new wxButton(panel, wxID_ANY, "Load Sets");
    hbox->Add(m_playerLoadSetsBtn, 0, wxLEFT, 10);

    vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);

    m_playerResultList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                      wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);

    m_playerResultList->InsertColumn(0, "ID",             wxLIST_FORMAT_LEFT, 80);
    m_playerResultList->InsertColumn(1, "Name",           wxLIST_FORMAT_LEFT, 140);
    m_playerResultList->InsertColumn(2, "Main",           wxLIST_FORMAT_LEFT, 80);
    m_playerResultList->InsertColumn(3, "Played",         wxLIST_FORMAT_RIGHT, 80);
    m_playerResultList->InsertColumn(4, "Won",            wxLIST_FORMAT_RIGHT, 80);
    m_playerResultList->InsertColumn(5, "Win Rate (%)",   wxLIST_FORMAT_RIGHT, 100);

    vbox->Add(m_playerResultList, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 5);
    panel->SetSizer(vbox);

    m_playerDSChoice->Bind(wxEVT_CHOICE, &MainFrame::OnPlayerDSChoice, this);
    m_playerSearchBtn->Bind(wxEVT_BUTTON, &MainFrame::OnPlayerSearch, this);
    m_playerLoadBtn->Bind(wxEVT_BUTTON, &MainFrame::OnPlayerLoad, this);
    m_playerLoadSetsBtn->Bind(wxEVT_BUTTON, &MainFrame::OnPlayerLoadSets, this);

    return panel;
}

void MainFrame::OnPlayerDSChoice(wxCommandEvent&) {
    currentDS = (DataStructureChoice)m_playerDSChoice->GetSelection();
}

void MainFrame::OnPlayerSearch(wxCommandEvent&) {
    if (!setsLoaded) {
        wxMessageBox("You must press 'Load Sets' before searching for players or stats.",
            "Sets Not Loaded!", wxOK|wxICON_WARNING, this);
        return;
    }
    wxString query = m_playerSearchText->GetValue();
    m_playerResultList->DeleteAllItems();
    if (query.IsEmpty()) return;
    std::string q = std::string(query.mb_str());
    wxStopWatch watch;
    PlayerRecord record;
    bool found = false;
    if (currentDS == HASH_TABLE) {
        if (auto* rec = playerHash.SearchByName(q))    { record = *rec; found=true; }
        else if (auto* rec = playerHash.SearchByID(q)) { record = *rec; found=true; }
    } else {
        if (auto* rec = playerTrie.SearchExact(q))     { record = *rec; found=true; }
    }
    double ms = watch.Time();
    SetEfficiency(m_playerEfficiencyLabel, ms);

    if (!found) {
        m_playerResultList->InsertItem(0, "Not found");
        UpdateVisitedRowsCounter();
        return;
    }
    m_playerResultList->InsertItem(0, record.id);
    m_playerResultList->SetItem(0, 1, record.name);
    m_playerResultList->SetItem(0, 2, record.main_character);
    m_playerResultList->SetItem(0, 3, wxString::Format("%d", record.matches_played));
    m_playerResultList->SetItem(0, 4, wxString::Format("%d", record.matches_won));
    m_playerResultList->SetItem(0, 5, wxString::Format("%.2f", record.win_rate*100.0));

    UpdateVisitedRowsCounter();
}

void MainFrame::OnPlayerLoad(wxCommandEvent&) {
    m_playerResultList->DeleteAllItems();

    setsLoaded = false; // must reload sets after this
    wxStopWatch watch;
    BusyStart("Loading Players Only...");
    bool ok;
    if (currentDS == HASH_TABLE)
        ok = BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerHash);
    else
        ok = BackendDB_LoadAllPlayers(dbPath.ToStdString(), playerTrie);
    BusyEnd();
    if (!ok)
        wxMessageBox("Failed to load players!", "Error", wxOK|wxICON_ERROR, this);
    SetEfficiency(m_playerEfficiencyLabel, watch.Time());

    UpdateVisitedRowsCounter();
}

void MainFrame::OnPlayerLoadSets(wxCommandEvent&) {
    BusyStart("Loading Sets and Player Stats...");
    bool ok = BackendDB_LoadPlayerStats(dbPath.ToStdString(), playerHash, playerTrie);
    BusyEnd();
    if (!ok) {
        wxMessageBox("Failed to load set stats!\nAre players loaded already?", "Error", wxOK | wxICON_ERROR, this);
        setsLoaded = false;
        UpdateVisitedRowsCounter();
        return;
    }
    setsLoaded = true;
    wxMessageBox("Set information loaded! You can now search/view player stats.", "Success", wxOK|wxICON_INFORMATION, this);

    // Immediately fill the list with all players with stats!
    m_playerResultList->DeleteAllItems();
    size_t maxRecords = 1000000;
    std::vector<PlayerRecord> batch;
    if (currentDS == HASH_TABLE)
        batch = playerHash.GetFirstNRecords(maxRecords);
    else
        for (auto* pr : playerTrie.GetFirstNRecords(maxRecords)) batch.push_back(*pr);

    size_t i = 0;
    for (const auto& rec : batch) {
        if (rec.matches_won < 1)
            continue; // Only show players with one or more wins
        m_playerResultList->InsertItem(i, rec.id);
        m_playerResultList->SetItem(i, 1, rec.name);
        m_playerResultList->SetItem(i, 2, rec.main_character);
        m_playerResultList->SetItem(i, 3, wxString::Format("%d", rec.matches_played));
        m_playerResultList->SetItem(i, 4, wxString::Format("%d", rec.matches_won));
        m_playerResultList->SetItem(i, 5, wxString::Format("%.2f", rec.win_rate*100.0));
        ++i;
    }
    UpdateVisitedRowsCounter();
}

//----------------- HEAD-TO-HEAD TAB ----------------------
wxPanel* MainFrame::CreateHeadToHeadPanel(wxWindow* parent) {
    wxPanel* panel = new wxPanel(parent);
    auto* vbox = new wxBoxSizer(wxVERTICAL);

    auto* topBox = new wxBoxSizer(wxHORIZONTAL);
    m_headDSChoice = new wxChoice(panel, wxID_ANY);
    m_headDSChoice->Append("Hash Table"); m_headDSChoice->Append("Trie");
    m_headDSChoice->SetSelection(0);

    topBox->Add(new wxStaticText(panel, wxID_ANY, "Data Structure:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    topBox->Add(m_headDSChoice, 0, wxRIGHT, 15);
    topBox->AddStretchSpacer();
    m_headEfficiencyLabel = new wxStaticText(panel, wxID_ANY, "Efficiency: ---");
    topBox->Add(m_headEfficiencyLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);

    vbox->Add(topBox, 0, wxEXPAND | wxALL, 5);

    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "Player 1:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_headP1Text = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_headP1Text, 1, wxRIGHT, 10);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "Player 2:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_headP2Text = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_headP2Text, 1, wxRIGHT, 10);
    m_headCompareBtn = new wxButton(panel, wxID_ANY, "Compare");
    hbox->Add(m_headCompareBtn, 0);
    vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);

    m_headResultList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
    m_headResultList->InsertColumn(0, "Metric", wxLIST_FORMAT_LEFT, 180);
    m_headResultList->InsertColumn(1, "Player 1", wxLIST_FORMAT_LEFT, 120);
    m_headResultList->InsertColumn(2, "Player 2", wxLIST_FORMAT_LEFT, 120);
    vbox->Add(m_headResultList, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 5);

    panel->SetSizer(vbox);

    m_headDSChoice->Bind(wxEVT_CHOICE, &MainFrame::OnHeadDSChoice, this);
    m_headCompareBtn->Bind(wxEVT_BUTTON, &MainFrame::OnHeadCompare, this);

    return panel;
}
void MainFrame::OnHeadDSChoice(wxCommandEvent&) { currentDS = (DataStructureChoice)m_headDSChoice->GetSelection(); }
void MainFrame::OnHeadCompare(wxCommandEvent&) {
    if (!setsLoaded) {
        wxMessageBox("You must press 'Load Sets' before comparing players.",
            "Sets Not Loaded!", wxOK|wxICON_WARNING, this);
        return;
    }
    wxString q1 = m_headP1Text->GetValue();
    wxString q2 = m_headP2Text->GetValue();
    m_headResultList->DeleteAllItems();
    wxStopWatch watch;
    PlayerRecord rec1, rec2; bool f1=false, f2=false;
    std::string s1 = q1.ToStdString(), s2 = q2.ToStdString();
    if (currentDS == HASH_TABLE) {
        if (auto* r = playerHash.SearchByName(s1)) { rec1=*r; f1=true; } else if (auto* r=playerHash.SearchByID(s1)) { rec1=*r; f1=true; }
        if (auto* r = playerHash.SearchByName(s2)) { rec2=*r; f2=true; }
        if (auto* r = playerHash.SearchByID(s2)) { rec2=*r; f2=true; }
    } else {
        if (auto* r = playerTrie.SearchExact(s1)) { rec1=*r; f1=true; }
        if (auto* r = playerTrie.SearchExact(s2)) { rec2=*r; f2=true; }
    }
    double ms = watch.Time();
    SetEfficiency(m_headEfficiencyLabel, ms);
    if (!f1 || !f2) {
        m_headResultList->InsertItem(0, "One or both players not found.");
        UpdateVisitedRowsCounter();
        return;
    }

    m_headResultList->InsertItem(0, "ID");             m_headResultList->SetItem(0,1,rec1.id);   m_headResultList->SetItem(0,2,rec2.id);
    m_headResultList->InsertItem(1, "Name");           m_headResultList->SetItem(1,1,rec1.name); m_headResultList->SetItem(1,2,rec2.name);
    m_headResultList->InsertItem(2, "Main");           m_headResultList->SetItem(2,1,rec1.main_character); m_headResultList->SetItem(2,2,rec2.main_character);
    m_headResultList->InsertItem(3, "Played");         m_headResultList->SetItem(3,1,wxString::Format("%d",rec1.matches_played)); m_headResultList->SetItem(3,2,wxString::Format("%d",rec2.matches_played));
    m_headResultList->InsertItem(4, "Won");            m_headResultList->SetItem(4,1,wxString::Format("%d",rec1.matches_won));    m_headResultList->SetItem(4,2,wxString::Format("%d",rec2.matches_won));
    m_headResultList->InsertItem(5, "Win Rate");       m_headResultList->SetItem(5,1,wxString::Format("%.2f%%",rec1.win_rate*100)); m_headResultList->SetItem(5,2,wxString::Format("%.2f%%",rec2.win_rate*100));

    UpdateVisitedRowsCounter();
}

//--------------- CHARACTER MATCHUP TAB ---------------
wxPanel* MainFrame::CreateCharacterMatchupPanel(wxWindow* parent) {
    wxPanel* panel = new wxPanel(parent);
    auto* vbox = new wxBoxSizer(wxVERTICAL);

    auto* topBox = new wxBoxSizer(wxHORIZONTAL);
    m_charDSChoice = new wxChoice(panel, wxID_ANY);
    m_charDSChoice->Append("Hash Table"); m_charDSChoice->Append("Trie");
    m_charDSChoice->SetSelection(0);

    topBox->Add(new wxStaticText(panel, wxID_ANY, "Data Structure:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    topBox->Add(m_charDSChoice, 0, wxRIGHT, 15);
    topBox->AddStretchSpacer();
    m_charEfficiencyLabel = new wxStaticText(panel, wxID_ANY, "Efficiency: ---");
    topBox->Add(m_charEfficiencyLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);

    vbox->Add(topBox, 0, wxEXPAND | wxALL, 5);

    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "Character 1:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_char1Text = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_char1Text, 1, wxRIGHT, 10);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "vs"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_char2Text = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_char2Text, 1, wxRIGHT, 10);
    m_charAnalyzeBtn = new wxButton(panel, wxID_ANY, "Analyze");
    hbox->Add(m_charAnalyzeBtn, 0, wxRIGHT, 10);
    m_charLoadAllBtn = new wxButton(panel, wxID_ANY, "Load All Characters");
    hbox->Add(m_charLoadAllBtn, 0);
    vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);

    m_charResultList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
    m_charResultList->InsertColumn(0, "Character", wxLIST_FORMAT_LEFT, 140);
    m_charResultList->InsertColumn(1, "Played", wxLIST_FORMAT_RIGHT, 100);
    m_charResultList->InsertColumn(2, "Won", wxLIST_FORMAT_RIGHT, 100);
    m_charResultList->InsertColumn(3, "Win Rate (%)", wxLIST_FORMAT_RIGHT, 110);

    vbox->Add(m_charResultList, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 5);
    panel->SetSizer(vbox);

    m_charDSChoice->Bind(wxEVT_CHOICE, &MainFrame::OnCharDSChoice, this);
    m_charAnalyzeBtn->Bind(wxEVT_BUTTON, &MainFrame::OnCharAnalyze, this);
    m_charLoadAllBtn->Bind(wxEVT_BUTTON, &MainFrame::OnCharLoadAll, this);

    return panel;
}
void MainFrame::OnCharDSChoice(wxCommandEvent&) { currentDS = (DataStructureChoice)m_charDSChoice->GetSelection(); }
void MainFrame::OnCharAnalyze(wxCommandEvent&) {
    if (!setsLoaded) {
        wxMessageBox("You must press 'Load Sets' before using character stats.",
            "Sets Not Loaded!", wxOK|wxICON_WARNING, this);
        return;
    }
    m_charResultList->DeleteAllItems();
    std::string char1 = m_char1Text->GetValue().ToStdString();
    std::string char2 = m_char2Text->GetValue().ToStdString();
    wxStopWatch watch;
    if (char1.empty() || char2.empty()) {
        m_charResultList->InsertItem(0, "Enter both character names");
        SetEfficiency(m_charEfficiencyLabel, watch.Time());
        UpdateVisitedRowsCounter();
        return;
    }
    int char1_play=0, char1_win=0, char2_play=0, char2_win=0;
    std::vector<PlayerRecord> records;
    if (currentDS == HASH_TABLE)      records = playerHash.GetFirstNRecords(100000);
    else for (auto* r : playerTrie.GetFirstNRecords(100000)) records.push_back(*r);

    for (const auto& rec : records) {
        if (rec.main_character == char1) { char1_play += rec.matches_played; char1_win += rec.matches_won; }
        if (rec.main_character == char2) { char2_play += rec.matches_played; char2_win += rec.matches_won; }
    }
    m_charResultList->InsertItem(0, char1);
    m_charResultList->SetItem(0,1, wxString::Format("%d", char1_play));
    m_charResultList->SetItem(0,2, wxString::Format("%d", char1_win));
    m_charResultList->SetItem(0,3, wxString::Format("%.2f", char1_play?100.0*char1_win/char1_play:0.0));
    m_charResultList->InsertItem(1, char2);
    m_charResultList->SetItem(1,1, wxString::Format("%d", char2_play));
    m_charResultList->SetItem(1,2, wxString::Format("%d", char2_win));
    m_charResultList->SetItem(1,3, wxString::Format("%.2f", char2_play?100.0*char2_win/char2_play:0.0));
    SetEfficiency(m_charEfficiencyLabel, watch.Time());

    UpdateVisitedRowsCounter();
}
void MainFrame::OnCharLoadAll(wxCommandEvent&) {
    if (!setsLoaded) {
        wxMessageBox("You must press 'Load Sets' before using character stats.",
            "Sets Not Loaded!", wxOK|wxICON_WARNING, this);
        return;
    }
    m_charResultList->DeleteAllItems();
    wxStopWatch watch;
    std::map<std::string,std::tuple<int,int>> c_stats;
    std::vector<PlayerRecord> recs;
    if (currentDS == HASH_TABLE) recs = playerHash.GetFirstNRecords(1000000);
    else for (auto* r: playerTrie.GetFirstNRecords(1000000)) recs.push_back(*r);

    for (const auto& rec: recs) {
        auto& tup = c_stats[rec.main_character];
        std::get<0>(tup) += rec.matches_played;
        std::get<1>(tup) += rec.matches_won;
    }
    int i=0;
    for (const auto& kv : c_stats) {
        const std::string& k = kv.first;
        int played = std::get<0>(kv.second);
        int won    = std::get<1>(kv.second);
        m_charResultList->InsertItem(i, k);
        m_charResultList->SetItem(i,1, wxString::Format("%d", played));
        m_charResultList->SetItem(i,2, wxString::Format("%d", won));
        m_charResultList->SetItem(i,3, wxString::Format("%.2f", played?100.0*won/played:0.0));
        ++i;
    }
    SetEfficiency(m_charEfficiencyLabel, watch.Time());
    UpdateVisitedRowsCounter();
}


//--------------- STAGE ANALYSIS TAB ---------------
wxPanel* MainFrame::CreateStageAnalysisPanel(wxWindow* parent) {
    wxPanel* panel = new wxPanel(parent);
    auto* vbox = new wxBoxSizer(wxVERTICAL);

    auto* topBox = new wxBoxSizer(wxHORIZONTAL);
    m_stageDSChoice = new wxChoice(panel, wxID_ANY);
    m_stageDSChoice->Append("Hash Table"); m_stageDSChoice->Append("Trie");
    m_stageDSChoice->SetSelection(0);

    topBox->Add(new wxStaticText(panel, wxID_ANY, "Data Structure:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    topBox->Add(m_stageDSChoice, 0, wxRIGHT, 15);
    topBox->AddStretchSpacer();
    m_stageEfficiencyLabel = new wxStaticText(panel, wxID_ANY, "Efficiency: ---");
    topBox->Add(m_stageEfficiencyLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);

    vbox->Add(topBox, 0, wxEXPAND | wxALL, 5);

    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    hbox->Add(new wxStaticText(panel, wxID_ANY, "Stage:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_stageText = new wxTextCtrl(panel, wxID_ANY);
    hbox->Add(m_stageText, 1, wxRIGHT, 10);
    m_stageAnalyzeBtn = new wxButton(panel, wxID_ANY, "Analyze");
    hbox->Add(m_stageAnalyzeBtn, 0);
    vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);

    m_stageResultList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);

    m_stageResultList->InsertColumn(0, "Player/Character", wxLIST_FORMAT_LEFT, 200);
    m_stageResultList->InsertColumn(1, "Win Rate (%)", wxLIST_FORMAT_RIGHT, 120);

    vbox->Add(m_stageResultList, 1, wxEXPAND | wxLEFT|wxRIGHT|wxBOTTOM, 5);
    panel->SetSizer(vbox);

    m_stageDSChoice->Bind(wxEVT_CHOICE, &MainFrame::OnStageDSChoice, this);
    m_stageAnalyzeBtn->Bind(wxEVT_BUTTON, &MainFrame::OnStageAnalyze, this);

    return panel;
}
void MainFrame::OnStageDSChoice(wxCommandEvent&) { currentDS = (DataStructureChoice)m_stageDSChoice->GetSelection(); }
void MainFrame::OnStageAnalyze(wxCommandEvent&) {
    if (!setsLoaded) {
        wxMessageBox("You must press 'Load Sets' before using stage analysis.",
            "Sets Not Loaded!", wxOK|wxICON_WARNING, this);
        return;
    }
    m_stageResultList->DeleteAllItems();
    wxStopWatch watch;
    std::vector<PlayerRecord> recs;
    if (currentDS == HASH_TABLE) recs = playerHash.GetFirstNRecords(100000);
    else for (auto* r: playerTrie.GetFirstNRecords(100000)) recs.push_back(*r);

    int i=0;
    for (const auto& rec: recs) {
        wxString row = rec.name + " (" + rec.main_character + ")";
        m_stageResultList->InsertItem(i, row);
        m_stageResultList->SetItem(i,1, wxString::Format("%.2f", rec.win_rate*100.0));
        ++i;
    }
    SetEfficiency(m_stageEfficiencyLabel, watch.Time());
    UpdateVisitedRowsCounter();
}


//---------------------- MENU HANDLERS --------------------
void MainFrame::OnMenuExport(wxCommandEvent&) {
    wxFileDialog saveFileDialog(this, _("Export Results"), "", "",
        "CSV files (*.csv)|*.csv|Text files (*.txt)|*.txt|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;
    wxString path = saveFileDialog.GetPath();
    wxLogMessage("Exported to %s", path);
}
void MainFrame::OnMenuExit(wxCommandEvent&) { Close(true); }
void MainFrame::OnAbout(wxCommandEvent&) {
    wxMessageBox("Super Smash Bros. Ultimate Data Analyzer\n"
        "Team Project GUI using wxWidgets, C++17\n"
        "Now with real DB integration and performance metrics.",
        "About", wxOK | wxICON_INFORMATION, this);
}
