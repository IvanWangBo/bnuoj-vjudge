/* 
 * File:   CFJudger.cpp
 * Author: 51isoft
 * 
 * Created on 2014年2月1日, 上午12:02
 */

#include "CFJudger.h"

/**
 * Create a CF Judger
 * @param _info Should be a pointer of a JudgerInfo
 */
CFJudger::CFJudger(JudgerInfo * _info) : VirtualJudger(_info) {
    socket->sendMessage(CONFIG->GetJudge_connect_string() + "\nCodeForces");
    
    language_table["1"]  = "1";
    language_table["2"]  = "10";
    language_table["3"]  = "5";
    language_table["4"]  = "4";
    language_table["5"]  = "7";
    language_table["6"]  = "9";
    language_table["9"]  = "8";
    language_table["12"] = "2";
}

CFJudger::~CFJudger() {
}


/**
 * Get Csrf token
 * @param url   URL you need to get csrf token
 * @return Csrf token for current session
 */
string CFJudger::getCsrfParams(string url) {
    
    prepareCurl();    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    performCurl();
    
    // Format: <meta name="X-Csrf-Token" content="21d5gfa7bceebhe5f45d6f5eb7930ea7"/>
    string html = loadAllFromFile(tmpfilename);
    string csrf;
    if (!RE2::PartialMatch(html, "X-Csrf-Token.*content=\"(.*?)\"", &csrf)) {
        throw Exception("Failed to get Csrf token.");
    }
    return csrf;
}

/**
 * Copied from CF's js
 */
int CFJudger::calculatetta(string a) {
    int b = 0;
    for(int c = 0; c < a.length(); ++c){
        b = (b + (c + 1) * (c + 2) * a[c]) % 1009;
        if ( c % 3 == 0) ++b;
        if ( c % 2 == 0) b *= 2;
        if ( c > 0) b -= ((int)(a[c / 2] / 2)) * (b % 5);
        while (b < 0) b += 1009;
        while (b >= 1009) b -= 1009;
    } 
    return b;
}

/**
 * CF needs a _tta for every request, calculated according to _COOKIE[39ce7]
 * @return _tta
 */
string CFJudger::getttaValue() {
    string cookies = loadAllFromFile(cookiefilename);
    string magic;
    
    if (!RE2::PartialMatch(cookies, "39ce7\\t(.*)", &magic)) {
        throw Exception("Failed to get magic cookie for tta.");
    }
    string tta = intToString(calculatetta(magic));
    log("tta Value: " + tta);
    return tta;
}

/**
 * Login to CodeForces
 */
void CFJudger::login() {
    string csrf = getCsrfParams("http://codeforces.com/enter");
    
    prepareCurl();
    curl_easy_setopt(curl, CURLOPT_REFERER, "http://codeforces.com/enter");
    curl_easy_setopt(curl, CURLOPT_URL, "http://codeforces.com/enter");
    string post = (string)"csrf_token=" + csrf +
                "&action=enter&handle=" + info->GetUsername() +
                "&password=" + info->GetPassword() +
                "&_tta=" + getttaValue();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
    performCurl();
    
    // check login status
    string html = loadAllFromFile(tmpfilename);
    if (html.find("Invalid handle or password") != string::npos) {
        throw Exception("Login failed!");
    }
}

string CFJudger::getActionUrl() {
    prepareCurl();
    curl_easy_setopt(curl, CURLOPT_URL, "http://codeforces.com/problemset/submit");
    performCurl();
    
    string html = loadAllFromFile(tmpfilename);
    string url;
    if (!RE2::PartialMatch(html, "<form class=.*submit-form.*action=\"(.*?)\"", &url)) {
        throw Exception("Failed to get action url.");
    }
    return url;
}

/**
 * Submit a run
 * @param bott      Bott file for Run info
 * @return Submit status
 */
int CFJudger::submit(Bott * bott) {
    string csrf = getCsrfParams("http://codeforces.com/problemset/submit");
    
    // prepare cid and pid from vid
    string contest, problem;
    if (!RE2::PartialMatch(bott->Getvid(), "([0-9]*)(.*)", &contest, &problem)) {
        throw Exception("Invalid vid.");
    }
    string source = bott->Getsrc();
    // add random extra spaces in the end to avoid same code error
    srand(time(NULL));
    source += '\n';
    while (rand()%120) source+=' ';
    
    // prepare form for post
    struct curl_httppost * formpost = NULL;
    struct curl_httppost * lastptr  = NULL;
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "action",
                 CURLFORM_COPYCONTENTS, "submitSolutionFormSubmitted",
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "contestId",
                 CURLFORM_COPYCONTENTS, contest.c_str(),
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "submittedProblemIndex",
                 CURLFORM_COPYCONTENTS, problem.c_str(),
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "programTypeId",
                 CURLFORM_COPYCONTENTS, bott->Getlanguage().c_str(),
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "source",
                 CURLFORM_COPYCONTENTS, source.c_str(),
                 CURLFORM_END);
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "_tta",
                 CURLFORM_COPYCONTENTS, getttaValue().c_str(),
                 CURLFORM_END);
    
    prepareCurl();
    curl_easy_setopt(curl, CURLOPT_URL, ((string)"http://codeforces.com/problemset/submit?csrf_token=" + csrf).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    performCurl();
    curl_formfree(formpost);
    
    // check submit status
    string html = loadAllFromFile(tmpfilename);
    if (html.find("You have submitted exactly the same code before") != string::npos) {
        return VirtualJudger::SUBMIT_SAME_CODE;
    } else if (html.find("Choose valid language") != string::npos) {
        return VirtualJudger::SUBMIT_INVALID_LANGUAGE;
    } else if (html.find("<span class=\"error for__source\">") != string::npos) {
        return VirtualJudger::SUBMIT_COMPILE_ERROR;
    }
    return VirtualJudger::SUBMIT_NORMAL;
}

/**
 * Get result and related info
 * @param bott  Original Bott info
 * @return Result Bott file
 */
Bott * CFJudger::getStatus(Bott * bott) {
    time_t begin_time = time(NULL);
    
    Bott * result_bott;
    while (true) {
        // check wait time
        if (time(NULL) - begin_time > info->GetMax_wait_time()) break;
        
        prepareCurl();
        curl_easy_setopt(curl, CURLOPT_URL, ((string)"http://codeforces.com/submissions/" + info->GetUsername()).c_str());
        performCurl();
        
        string html = loadAllFromFile(tmpfilename);
        string status;
        string runid, result, time_used, memory_used;
        // get first result row
        if (!RE2::PartialMatch(html, "(?s)<tr.*first-row.*?(<tr.*?</tr>)", &status)) {
            throw Exception("Failed to get status row.");
        }
        // get result
        if (!RE2::PartialMatch(status, "(?s)status-cell.*?>(.*?)</td>", &result)) {
            throw Exception("Failed to get current result.");
        }
        
        if (result.find("Waiting") == string::npos &&
                result.find("Running") == string::npos &&
                result.find("Judging") == string::npos &&
                result.find("queue") == string::npos &&
                result.find("Compiling") == string::npos &&
                result.length()>6) {
            // if result if final, get details
            if (!RE2::PartialMatch(status,
                                   "(?s)data-submission-id=\"([0-9]*).*status-cell.*?>(.*?)</td>.*time-consumed-cell.*?>(.*?) ms.*memory-consumed-cell.*?>(.*?) KB",
                                   &runid, &result, &time_used, &memory_used)) {
                throw Exception("Failed to parse details from status row.");
            }
            result_bott = new Bott;
            result_bott->Settype(RESULT_REPORT);
            result_bott->Setresult(convertResult(result));
            result_bott->Settime_used(trim(time_used));
            result_bott->Setmemory_used(trim(memory_used));
            result_bott->Setremote_runid(runid);
            break;
        }
    }
    
    string contest;
    // no need to check fail or not, since submit function has already done it
    RE2::PartialMatch(bott->Getvid(), "([0-9]*)", &contest);
    if (result_bott->Getresult() != "Accepted" && result_bott->Getresult() != "Compile Error")  {
        result_bott->Setce_info(getVerdict(contest, result_bott->Getremote_runid()));
    }
    throw Exception("Testing");
    return NULL;
}

/**
 * Convert CF result text to local ones, keep consistency
 * @param result Original result
 * @return Converted local result
 */
string CFJudger::convertResult(string result) {
    if (result.find("Compilation error") != string::npos) return "Compile Error";
    if (result.find("Wrong answer") != string::npos) return "Wrong Answer";
    if (result.find("Runtime error") != string::npos) return "Runtime Error";
    if (result.find("Time limit exceeded") != string::npos) return "Time Limit Exceed";
    if (result.find("Idleness") != string::npos || result.find("Memory limit exceeded") != string::npos) return "Memory Limit Exceed";
    if (result.find("Denial of") != string::npos || result.find("Judgement failed") != string::npos) return "Judge Error";
    return trim(result);
}

/**
 * Get compile error info from CodeForces
 * @param bott      Result bott file
 * @return Compile error info
 */
string CFJudger::getCEinfo(Bott * bott) {
    
}

/**
 * Get run details from Codeforces
 * @param contest       Contest ID
 * @param runid         Remote runid
 * @return Verdict details
 */
string CFJudger::getVerdict(string contest, string runid) {
    prepareCurl();
    curl_easy_setopt(curl, CURLOPT_URL, ((string)"http://codeforces.com/contest/" + contest + "/submission/" + runid).c_str());
    performCurl();
    
    string html = loadAllFromFile(tmpfilename);
}