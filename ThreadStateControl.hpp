#ifndef __THREAD_STATE_CONTROL_hpp__
#define __THREAD_STATE_CONTROL_hpp__

#include <chrono>
#include <string>
#include <QThread>
#include "SendMessageManager.hpp"
#include "ThreadWhisper.hpp"
#include "ThreadLLM.hpp"
#include "ThreadProcessImage.hpp"
#include "utility_KebbiMotion.hpp" 
#include <nlohmann/json.hpp>
#include "VideoWindow.hpp"
#include "Setting.hpp"

using namespace std;

//------------------------------------------------------------------
// BranchCondition: one entry in a State's optional "Branches" list in JSON.
// If the patient's spoken answer contains ANY keyword in v_str_Keywords,
// the state machine jumps to iTargetStateIndex instead of the state's
// normal iNextStateIndex.
// Branches are checked in array order; the FIRST matching branch wins.
//------------------------------------------------------------------
struct BranchCondition
{
    vector<string> v_str_Keywords;
    int iTargetStateIndex = -1;
};

inline void from_json(const nlohmann::json& j, BranchCondition& b)
{
    j.at("Keywords").get_to(b.v_str_Keywords);
    j.at("TargetStateIndex").get_to(b.iTargetStateIndex);
}

inline void to_json(nlohmann::json& j, const BranchCondition& b)
{
    j = nlohmann::json{
        {"Keywords", b.v_str_Keywords},
        {"TargetStateIndex", b.iTargetStateIndex}
    };
}

struct State
{
    //StateSetting
    int iStateIndex;
    string m_strStateName;
    int iDurationLimitSeconds;     //JSON supports int but not chrono::seconds
    string m_strFirstSentence;          //The first sentence to speak when enter this state
    string sFace;
    string sMotion;
    string sStateType;
    vector<string> vSmallMotion;
    int iNextStateIndex = -1;  //bug proofing
    vector<string> v_str_KeyWordMoveToNextState;
    vector<string> v_str_Action;

    //Branching (OPTIONAL in JSON). If a state's JSON has no "Branches" array,
    //v_Branches stays empty and the state behaves exactly as before -- zero changes
    //needed for states that don't need branching.
    vector<BranchCondition> v_Branches;
    int iWrongAnswerStateIndex = -1;   //Where to go if Branches exist but none matched. -1 = fall back to iNextStateIndex.

    //Dynarmic data
    chrono::time_point<std::chrono::system_clock> m_Start_time;
    bool bInitial = true;
    bool bWaitForTTSComplete = true;
    bool bEndState = false;
    std::vector<std::string> message_history;
    int iNextStateIndexOverride = -1;   //RUNTIME ONLY -- never read from or written to JSON.
                                         //Set by the branch-matching logic in run(), consumed once
                                         //at the actual state transition, then reset to -1.

    chrono::seconds m_secDurationLimit;     //Converted from iDurationLimitSeconds

    //Special variables for some states
    int iStage = 0;             //Only wok for the Ask Dance state. Stage 0 is conversation, Stage 1 is dance performance.
    string sImageFileName;
};

//Custom from_json/to_json (instead of NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE) so that
//"Branches" and "WrongAnswerStateIndex" can be OPTIONAL. NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
//uses j.at(...) internally for every listed field, which would throw and abort loading
//for any existing state JSON that doesn't have these two new keys. j.value(key, default)
//below means old JSON files keep working with zero edits.
inline void from_json(const nlohmann::json& j, State& s)
{
    j.at("iStateIndex").get_to(s.iStateIndex);
    j.at("m_strStateName").get_to(s.m_strStateName);
    j.at("iDurationLimitSeconds").get_to(s.iDurationLimitSeconds);
    j.at("m_strFirstSentence").get_to(s.m_strFirstSentence);
    j.at("iNextStateIndex").get_to(s.iNextStateIndex);
    j.at("sFace").get_to(s.sFace);
    j.at("sMotion").get_to(s.sMotion);
    j.at("vSmallMotion").get_to(s.vSmallMotion);
    j.at("v_str_KeyWordMoveToNextState").get_to(s.v_str_KeyWordMoveToNextState);
    j.at("v_str_Action").get_to(s.v_str_Action);
    j.at("sStateType").get_to(s.sStateType);
    j.at("sImageFileName").get_to(s.sImageFileName);

    //Optional branching fields
    s.v_Branches = j.value("Branches", vector<BranchCondition>{});
    s.iWrongAnswerStateIndex = j.value("WrongAnswerStateIndex", -1);
}

inline void to_json(nlohmann::json& j, const State& s)
{
    j = nlohmann::json{
        {"iStateIndex", s.iStateIndex},
        {"m_strStateName", s.m_strStateName},
        {"iDurationLimitSeconds", s.iDurationLimitSeconds},
        {"m_strFirstSentence", s.m_strFirstSentence},
        {"iNextStateIndex", s.iNextStateIndex},
        {"sFace", s.sFace},
        {"sMotion", s.sMotion},
        {"vSmallMotion", s.vSmallMotion},
        {"v_str_KeyWordMoveToNextState", s.v_str_KeyWordMoveToNextState},
        {"v_str_Action", s.v_str_Action},
        {"sStateType", s.sStateType},
        {"sImageFileName", s.sImageFileName},
        {"Branches", s.v_Branches},
        {"WrongAnswerStateIndex", s.iWrongAnswerStateIndex}
    };
}

class ThreadLLM; //Because ThreadLLM.hpp and ThreadStateControl.hpp include each other, I need to use forward declaration


class ThreadStateControl: public QThread
{
    Q_OBJECT

public:
    ThreadStateControl();
    ~ThreadStateControl();

    bool b_WhileLoop = true;
    void InitializeStates();
    void NextState();
    SendMessageManager *m_pSendMessageManager;
    ThreadWhisper *mpThreadWhisper;
    ThreadLLM *mpThreadLLM;
    ThreadProcessImage *mpThreadProcessImage;
    void NotifyEvent(string description, chrono::time_point<chrono::system_clock> timestamp, string sLLMResult = "");
    condition_variable cond_var_state_control;
    void SetIntialStateIndex(int index);

    VideoWindow* pVideoWindow = nullptr;
    void SetSettingFile(const QString &filePath);

signals:
    void playVideoRequest(const QString& videoPath);
    void playImageRequest(const QString& imagePath);

protected:
    void run();
    string ConvertMessageHistoryToString(vector<string> message_history);
    void DumpHistoryMessages(vector<string> messages);
    string ReplaceVariables(string sentence);

    vector<State> mStates;
    int m_iNumberOfStates = 10;
    int m_iStateIndex = 0;
    int m_iPreviousStateIndex = -1;   //Actual previous state visited. Needed because once branches
                                       //exist, the previous state is not always "m_iStateIndex - 1".
                                       //Used by the "AccumulateConversation" action.

    bool mbTTSComplete = false;
    bool mbWaitForTTSComplete = false;
    chrono::time_point<chrono::system_clock> mtimestamp_TTSComplete;
    chrono::time_point<chrono::system_clock> mtimestamp_VideoComplete;


    bool mbLLMResult = false;
    bool mbWaitForLLMResult = false;
    chrono::time_point<chrono::system_clock> mtimestamp_LLMResult;
    string msLLMResult;
    int chosen_dance = 0;

    bool mbActivity_mbtx_Complete = false;
    chrono::time_point<chrono::system_clock> mtimestamp_Activity_mbtx_Complete;

    bool mbKeepSilent = false;
    bool mbReadyToChangeState = false;      //This variable is used to decide whether to chat with LLM in a loop.
    bool mbOldStateComplete = false;        //This varaible is used to check whether the time is up, or some keyword is detected.
    Setting msetting;

    string msPatientName = ""; //default name is "patient"
    string msPatientTitle = ""; //先生 or 小姐, default is empty string. It is determined by the LLM result of patient name. If the patient name ends with "先生", then the title is "先生". If the patient name ends with "小姐", then the title is "小姐". Otherwise, the title is "".
    string GetPatientName(string input_sentence);
};

#endif