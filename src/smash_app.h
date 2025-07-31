#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <string>
#include "backend.h"

//--------------------------------------------------
// GLOBAL flag for sets/stat hydration state
//--------------------------------------------------
extern bool setsLoaded;

//--------------------------------------------------
// Enum for user data structure selection
//--------------------------------------------------
enum DataStructureChoice {
    HASH_TABLE = 0,
    TRIE       = 1,
    BOTH       = 2
};

class MainFrame;

//--------------------------------------------------
// App Class
//--------------------------------------------------
class SmashApp : public wxApp {
public:
    virtual bool OnInit() override;
};

//--------------------------------------------------
// Main GUI Frame
//--------------------------------------------------
class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title);

private:
    // Global UI
    wxNotebook* notebook = nullptr;
    wxString dbPath;
    // Data structures
    PlayerHashTable playerHash;
    PlayerTrie playerTrie;
    // User choice for active data structure
    DataStructureChoice currentDS = HASH_TABLE;

    // ---- Counter now uses backend only ----
    void UpdateVisitedRowsCounter(); // declaration only; defined in .cpp

    //--------------------------------------------------
    // Load Data Tab Widgets
    //--------------------------------------------------
    wxChoice*      m_perfDSChoice         = nullptr;
    wxButton*      m_perfRunBtn           = nullptr;
    wxListCtrl*    m_perfResultList       = nullptr;
    wxStaticText*  m_perfEfficiencyLabel  = nullptr;
    wxStaticText*  m_perfStatusLabel      = nullptr;

    //--------------------------------------------------
    // Player Stats Tab Widgets
    //--------------------------------------------------
    wxChoice*      m_playerDSChoice       = nullptr;
    wxTextCtrl*    m_playerSearchText     = nullptr;
    wxButton*      m_playerSearchBtn      = nullptr;
    wxButton*      m_playerLoadBtn        = nullptr;
    wxButton*      m_playerLoadSetsBtn    = nullptr;
    wxListCtrl*    m_playerResultList     = nullptr;
    wxStaticText*  m_playerEfficiencyLabel = nullptr;

    //--------------------------------------------------
    // Head-to-Head Tab Widgets
    //--------------------------------------------------
    wxChoice*      m_headDSChoice         = nullptr;
    wxTextCtrl*    m_headP1Text           = nullptr;
    wxTextCtrl*    m_headP2Text           = nullptr;
    wxButton*      m_headCompareBtn       = nullptr;
    wxListCtrl*    m_headResultList       = nullptr;
    wxStaticText*  m_headEfficiencyLabel  = nullptr;

    //--------------------------------------------------
    // Character Matchup Tab Widgets
    //--------------------------------------------------
    wxChoice*      m_charDSChoice         = nullptr;
    wxTextCtrl*    m_char1Text            = nullptr;
    wxTextCtrl*    m_char2Text            = nullptr;
    wxButton*      m_charAnalyzeBtn       = nullptr;
    wxButton*      m_charLoadAllBtn       = nullptr;
    wxListCtrl*    m_charResultList       = nullptr;
    wxStaticText*  m_charEfficiencyLabel  = nullptr;

    //--------------------------------------------------
    // Stage Analysis Tab Widgets
    //--------------------------------------------------
    wxChoice*      m_stageDSChoice        = nullptr;
    wxTextCtrl*    m_stageText            = nullptr;
    wxButton*      m_stageAnalyzeBtn      = nullptr;
    wxListCtrl*    m_stageResultList      = nullptr;
    wxStaticText*  m_stageEfficiencyLabel = nullptr;

    // Busy/loading dialog
    wxDialog*      m_loadingDialog        = nullptr;

    //--------------------------------------------------
    // Panel creation helpers
    //--------------------------------------------------
    wxPanel* CreateLoadDataPanel(wxWindow* parent);
    wxPanel* CreatePlayerStatsPanel(wxWindow* parent);
    wxPanel* CreateHeadToHeadPanel(wxWindow* parent);
    wxPanel* CreateCharacterMatchupPanel(wxWindow* parent);
    wxPanel* CreateStageAnalysisPanel(wxWindow* parent);

    //--------------------------------------------------
    // Menu event handlers
    //--------------------------------------------------
    void OnMenuExport(wxCommandEvent& event);
    void OnMenuExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);

    //--------------------------------------------------
    // Tab event handlers
    //--------------------------------------------------
    void OnPerfRun(wxCommandEvent& event);
    void OnPerfDSChoice(wxCommandEvent& event);

    void OnPlayerDSChoice(wxCommandEvent& event);
    void OnPlayerSearch(wxCommandEvent& event);
    void OnPlayerLoad(wxCommandEvent& event);
    void OnPlayerLoadSets(wxCommandEvent& event);

    void OnHeadDSChoice(wxCommandEvent& event);
    void OnHeadCompare(wxCommandEvent& event);

    void OnCharDSChoice(wxCommandEvent& event);
    void OnCharAnalyze(wxCommandEvent& event);
    void OnCharLoadAll(wxCommandEvent& event);

    void OnStageDSChoice(wxCommandEvent& event);
    void OnStageAnalyze(wxCommandEvent& event);

    //--------------------------------------------------
    // Helpers
    //--------------------------------------------------
    void BusyStart(const wxString& label);
    void BusyEnd();
    void SetEfficiency(wxStaticText* label, double ms);

    wxDECLARE_EVENT_TABLE();
};
