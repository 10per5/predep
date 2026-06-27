#include "sys/process.h"
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
#endif

namespace process {

std::string shell()
{
#ifdef _WIN32
    return "cmd.exe";
#else
    return "/bin/sh";
#endif
}

std::string shell_cmd_flag()
{
#ifdef _WIN32
    return "/c";
#else
    return "-c";
#endif
}

#ifdef _WIN32

int run(const std::string &cmd, const std::vector<std::string> &args,
        const std::string &cwd)
{
    std::string cmdline = cmd;
    for (auto &a : args)
    {
        cmdline += " ";
        cmdline += a;
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, &cmdline[0], NULL, NULL, FALSE, 0, NULL,
                        cwd.empty() ? NULL : cwd.c_str(), &si, &pi))
        return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exit_code);
}

run_result run_with_err(const std::string &cmd, const std::vector<std::string> &args,
                        const std::string &cwd)
{
    std::string cmdline = cmd;
    for (auto &a : args)
    {
        cmdline += " ";
        cmdline += a;
    }

    HANDLE err_read, err_write;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&err_read, &err_write, &sa, 0))
        return {-1, {}};

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = err_write;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, &cmdline[0], NULL, NULL, TRUE, 0, NULL,
                        cwd.empty() ? NULL : cwd.c_str(), &si, &pi))
    {
        CloseHandle(err_read);
        CloseHandle(err_write);
        return {-1, {}};
    }

    CloseHandle(err_write);

    std::string err;
    char buf[4096];
    DWORD n;
    while (ReadFile(err_read, buf, sizeof(buf) - 1, &n, NULL) && n > 0)
    {
        buf[n] = '\0';
        err += buf;
    }
    CloseHandle(err_read);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (!err.empty() && err.back() == '\n')
        err.pop_back();

    return {static_cast<int>(exit_code), err};
}

std::string capture(const std::string &cmd, const std::vector<std::string> &args)
{
    std::string cmdline = cmd;
    for (auto &a : args)
    {
        cmdline += " ";
        cmdline += a;
    }

    HANDLE read_pipe, write_pipe;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
        return {};

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, &cmdline[0], NULL, NULL, TRUE, 0, NULL, NULL,
                        &si, &pi))
    {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return {};
    }

    CloseHandle(write_pipe);

    std::string result;
    char buf[4096];
    DWORD n;
    while (ReadFile(read_pipe, buf, sizeof(buf) - 1, &n, NULL) && n > 0)
    {
        buf[n] = '\0';
        result += buf;
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (!result.empty() && result.back() == '\n')
        result.pop_back();

    return result;
}

/*
 * run_elevated -- Windows-only UAC elevation via ShellExecuteExW with "runas" verb.
 *
 * Used in install_action.cpp and uninstall_action.cpp as the Windows analogue
 * of `sudo` on Unix. The directory-copy branch in copy_artifact also uses it
 * for xcopy when fs::copy fails on permission_denied.
 *
 * Rationale: Keeps elevation explicit and platform-specific rather than hidden
 * behind a generic platform::elevate() abstraction. Each call spawns cmd.exe
 * /c <command> via ShellExecuteExW, triggering the standard UAC prompt when
 * the process is not already elevated.
 *
 * Unlike sudo, Windows UAC (runas) does not cache credentials -- every call
 * prompts independently. There is no credential session to "clear" afterwards,
 * so no counterpart to sudo -K exists. On Windows 11 24H2+ the built-in `sudo`
 * command supports credential caching, but this code uses the traditional runas
 * verb for broader compatibility.
 *
 * Returns exit code (0 = success), -1 if elevation failed to launch.
 */
int run_elevated(const std::string &cmd, const std::vector<std::string> &args,
                 const std::string &cwd)
{
    std::string cmdline = cmd;
    for (auto &a : args)
    {
        cmdline += " ";
        cmdline += a;
    }

    int needed = MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, nullptr, 0);
    if (needed <= 0)
        return -1;
    std::wstring wcmdline(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, &wcmdline[0], needed);

    std::wstring wparams = L"/c " + wcmdline;

    std::wstring wdir;
    if (!cwd.empty())
    {
        int needed_dir = MultiByteToWideChar(CP_UTF8, 0, cwd.c_str(), -1, nullptr, 0);
        if (needed_dir > 0)
        {
            wdir.resize(needed_dir);
            MultiByteToWideChar(CP_UTF8, 0, cwd.c_str(), -1, &wdir[0], needed_dir);
        }
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = wparams.c_str();
    sei.lpDirectory = wdir.empty() ? nullptr : wdir.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei))
        return -1;

    if (sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);
        return static_cast<int>(exit_code);
    }

    return 0;
}

#else // POSIX

int run(const std::string &cmd, const std::vector<std::string> &args,
        const std::string &cwd)
{
    std::vector<const char *> argv;
    argv.push_back(cmd.c_str());
    for (auto &a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0)
    {
        if (!cwd.empty())
            chdir(cwd.c_str());
        execvp(cmd.c_str(), const_cast<char *const *>(argv.data()));
        auto errmsg = std::string("execvp: ") + strerror(errno);
        write(STDERR_FILENO, errmsg.c_str(), errmsg.size());
        _exit(127);
    }

    if (pid < 0)
        return -1;

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

run_result run_with_err(const std::string &cmd, const std::vector<std::string> &args,
                        const std::string &cwd)
{
    std::vector<const char *> argv;
    argv.push_back(cmd.c_str());
    for (auto &a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int err_pipe[2];
    if (pipe(err_pipe) != 0)
        return {-1, {}};

    pid_t pid = fork();
    if (pid == 0)
    {
        close(err_pipe[0]);
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);
        if (!cwd.empty())
            chdir(cwd.c_str());
        execvp(cmd.c_str(), const_cast<char *const *>(argv.data()));
        {
            auto errmsg = std::string("execvp: ") + strerror(errno);
            write(err_pipe[1], errmsg.c_str(), errmsg.size());
        }
        _exit(127);
    }

    close(err_pipe[1]);

    std::string err;
    char buf[4096];
    ssize_t n;
    while ((n = read(err_pipe[0], buf, sizeof(buf) - 1)) > 0)
    {
        buf[n] = '\0';
        err += buf;
    }
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!err.empty() && err.back() == '\n')
        err.pop_back();

    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {code, err};
}

std::string capture(const std::string &cmd, const std::vector<std::string> &args)
{
    std::vector<const char *> argv;
    argv.push_back(cmd.c_str());
    for (auto &a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return {};

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(cmd.c_str(), const_cast<char *const *>(argv.data()));
        _exit(127);
    }

    close(pipefd[1]);

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0)
    {
        buf[n] = '\0';
        result += buf;
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!result.empty() && result.back() == '\n')
        result.pop_back();

    return result;
}

#endif // _WIN32

}
