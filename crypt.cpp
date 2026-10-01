#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using namespace std;

static const string VERSION = "0.6.0";

enum class Backend {
    DNF,
    NPM,
    PIP,
    ANY,
    INVALID
};

struct Request {
    string package;
    Backend backend = Backend::DNF;
    bool valid = false;
};

struct Availability {
    bool dnf = false;
    bool npm = false;
    bool pip = false;
};

string backendName(Backend backend) {
    switch (backend) {
        case Backend::DNF: return "Fedora";
        case Backend::NPM: return "npm";
        case Backend::PIP: return "PyPI";
        case Backend::ANY: return "any";
        default: return "invalid";
    }
}

Backend parseBackend(string name) {
    transform(name.begin(), name.end(), name.begin(),
              [](unsigned char c) { return tolower(c); });

    if (name == "dnf" || name == "fedora")
        return Backend::DNF;

    if (name == "npm")
        return Backend::NPM;

    if (name == "pip" || name == "pypi" || name == "python")
        return Backend::PIP;

    if (name == "any")
        return Backend::ANY;

    return Backend::INVALID;
}

bool safeName(const string& name) {
    if (name.empty())
        return false;

    for (unsigned char c : name) {
        if (!isalnum(c) &&
            c != '-' &&
            c != '_' &&
            c != '.' &&
            c != '+' &&
            c != ':' &&
            c != '@' &&
            c != '/') {
            return false;
        }
    }

    return true;
}

int execute(const vector<string>& args) {
    if (args.empty())
        return 1;

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        vector<char*> argv;

        for (const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));

        argv.push_back(nullptr);

        execvp(argv[0], argv.data());

        perror("execvp");
        _exit(127);
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return 1;
}

string capture(const string& command) {
    string output;

    FILE* pipe = popen(command.c_str(), "r");

    if (!pipe)
        return output;

    char buffer[4096];

    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;

    pclose(pipe);

    return output;
}

string trim(string value) {
    while (!value.empty() &&
           isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());

    while (!value.empty() &&
           isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();

    return value;
}

vector<string> lines(const string& input) {
    vector<string> result;

    stringstream stream(input);
    string line;

    while (getline(stream, line)) {
        line = trim(line);

        if (!line.empty())
            result.push_back(line);
    }

    return result;
}

bool commandExists(const string& command) {
    string result =
        capture("command -v " + command + " 2>/dev/null");

    return !trim(result).empty();
}

bool dnfExists(const string& package) {
    if (!safeName(package))
        return false;

    string command =
        "dnf repoquery --latest-limit 1 "
        "--queryformat '%{name}' " +
        package +
        " 2>/dev/null";

    for (const auto& line : lines(capture(command))) {
        if (line == package)
            return true;
    }

    return false;
}

bool npmExists(const string& package) {
    if (!safeName(package))
        return false;

    if (!commandExists("npm"))
        return false;

    string command =
        "npm view " +
        package +
        " name --silent 2>/dev/null";

    string result = trim(capture(command));

    return result == package;
}

bool pipExists(const string& package) {
    if (!safeName(package))
        return false;

    if (!commandExists("python3"))
        return false;

    string command =
        "python3 -m pip index versions " +
        package +
        " 2>/dev/null";

    string result = capture(command);

    return !trim(result).empty();
}

Availability checkAvailability(const string& package) {
    Availability available;

    cout << "\nResolving " << package << "\n\n";

    available.dnf = dnfExists(package);

    cout << "Fedora   "
         << (available.dnf ? "✓ available" : "-")
         << "\n";

    /*
     * Native package priority is absolute.
     *
     * If Fedora has an exact match, we deliberately
     * avoid unnecessary external registry lookups.
     */
    if (available.dnf)
        return available;

    available.npm = npmExists(package);

    cout << "npm      "
         << (available.npm ? "✓ available" : "-")
         << "\n";

    available.pip = pipExists(package);

    cout << "PyPI     "
         << (available.pip ? "✓ available" : "-")
         << "\n";

    return available;
}

Backend resolveAny(const string& package) {
    cout << "\nCrypt Package Manager\n";
    cout << "────────────────────────\n";

    Availability available = checkAvailability(package);

    if (available.dnf) {
        cout << "\nSelected Fedora\n";
        cout << "Reason: native package available.\n";

        return Backend::DNF;
    }

    if (available.npm && !available.pip) {
        cout << "\nSelected npm\n";
        cout << "Reason: only exact external match.\n";

        return Backend::NPM;
    }

    if (!available.npm && available.pip) {
        cout << "\nSelected PyPI\n";
        cout << "Reason: only exact external match.\n";

        return Backend::PIP;
    }

    if (!available.npm && !available.pip) {
        cout << "\nPackage not found in supported sources.\n";

        return Backend::INVALID;
    }

    cout << "\nMultiple exact matches found:\n\n";
    cout << "[1] npm\n";
    cout << "[2] PyPI\n";
    cout << "[q] cancel\n\n";

    while (true) {
        cout << "Select source: ";
        cout.flush();

        string choice;

        if (!getline(cin, choice))
            return Backend::INVALID;

        choice = trim(choice);

        if (choice == "1" ||
            choice == "npm") {
            return Backend::NPM;
        }

        if (choice == "2" ||
            choice == "pip" ||
            choice == "pypi") {
            return Backend::PIP;
        }

        if (choice == "q" ||
            choice == "quit" ||
            choice == "cancel") {
            return Backend::INVALID;
        }

        cout << "Invalid selection.\n";
    }
}

bool confirm() {
    cout << "\nProceed? [Y/n] ";
    cout.flush();

    string answer;

    if (!getline(cin, answer))
        return false;

    answer = trim(answer);

    if (answer.empty())
        return true;

    transform(answer.begin(), answer.end(),
              answer.begin(),
              [](unsigned char c) { return tolower(c); });

    return answer == "y" ||
           answer == "yes";
}

filesystem::path cryptDataDir() {
    const char* home = getenv("HOME");

    if (!home)
        return "/tmp/crypt";

    return filesystem::path(home) /
           ".local" /
           "share" /
           "crypt";
}

filesystem::path cryptBinDir() {
    const char* home = getenv("HOME");

    if (!home)
        return "/tmp/crypt-bin";

    return filesystem::path(home) /
           ".local" /
           "bin";
}

filesystem::path npmPrefix() {
    return cryptDataDir() / "npm";
}

filesystem::path pipEnvironment() {
    return cryptDataDir() / "python";
}

bool ensureDirectory(const filesystem::path& path) {
    error_code error;

    filesystem::create_directories(path, error);

    return !error;
}

bool ensureNpmEnvironment() {
    if (!ensureDirectory(npmPrefix()))
        return false;

    if (!ensureDirectory(cryptBinDir()))
        return false;

    return true;
}

bool ensurePythonEnvironment() {
    filesystem::path env = pipEnvironment();
    filesystem::path python = env / "bin" / "python";

    if (filesystem::exists(python))
        return true;

    if (!ensureDirectory(cryptDataDir()))
        return false;

    cout << ":: Creating Crypt Python environment...\n";

    int result = execute({
        "python3",
        "-m",
        "venv",
        env.string()
    });

    if (result != 0) {
        cerr << "\nCrypt could not create its Python environment.\n";
        cerr << "Install Python venv support and try again.\n";
        return false;
    }

    return true;
}

bool exposeNpmExecutables() {
    filesystem::path source =
        npmPrefix() / "bin";

    filesystem::path destination =
        cryptBinDir();

    if (!filesystem::exists(source))
        return true;

    if (!ensureDirectory(destination))
        return false;

    error_code error;

    for (const auto& entry :
         filesystem::directory_iterator(source, error)) {

        if (error)
            return false;

        filesystem::path target =
            destination / entry.path().filename();

        error_code removeError;
        filesystem::remove(target, removeError);

        error_code linkError;

        filesystem::create_symlink(
            entry.path(),
            target,
            linkError
        );

        if (linkError)
            return false;
    }

    return true;
}

bool exposePythonExecutables() {
    filesystem::path source =
        pipEnvironment() / "bin";

    filesystem::path destination =
        cryptBinDir();

    if (!filesystem::exists(source))
        return true;

    if (!ensureDirectory(destination))
        return false;

    error_code error;

    for (const auto& entry :
         filesystem::directory_iterator(source, error)) {

        if (error)
            return false;

        string name =
            entry.path().filename().string();

        /*
         * Don't expose the venv's own Python/pip wrappers.
         */
        if (name == "python" ||
            name == "python3" ||
            name.rfind("python3.", 0) == 0 ||
            name == "pip" ||
            name == "pip3" ||
            name.rfind("pip3.", 0) == 0 ||
            name == "activate" ||
            name == "activate.csh" ||
            name == "activate.fish" ||
            name == "Activate.ps1") {
            continue;
        }

        filesystem::path target =
            destination / entry.path().filename();

        error_code removeError;
        filesystem::remove(target, removeError);

        error_code linkError;

        filesystem::create_symlink(
            entry.path(),
            target,
            linkError
        );

        if (linkError)
            return false;
    }

    return true;
}

int addPackage(const string& package,
               Backend backend) {

    cout << "\nCrypt Package Manager\n";
    cout << "────────────────────────\n\n";

    cout << "Package   " << package << "\n";
    cout << "Source    "
         << backendName(backend)
         << "\n";

    if (!confirm()) {
        cout << "\nCancelled.\n";
        return 0;
    }

    cout << "\n:: Installing...\n\n";

    int result = 1;

    if (backend == Backend::DNF) {
        result = execute({
            "dnf",
            "install",
            "-y",
            package
        });
    }

    else if (backend == Backend::NPM) {
        if (!commandExists("npm")) {
            cerr << "npm is not installed.\n";
            return 1;
        }

        if (!ensureNpmEnvironment()) {
            cerr << "Could not create Crypt npm environment.\n";
            return 1;
        }

        result = execute({
            "npm",
            "install",
            "--global",
            "--prefix",
            npmPrefix().string(),
            package
        });

        if (result == 0 &&
            !exposeNpmExecutables()) {
            cerr << "\nPackage installed, but Crypt could not expose its CLI.\n";
            return 1;
        }
    }

    else if (backend == Backend::PIP) {
        if (!ensurePythonEnvironment())
            return 1;

        filesystem::path python =
            pipEnvironment() /
            "bin" /
            "python";

        result = execute({
            python.string(),
            "-m",
            "pip",
            "install",
            package
        });

        if (result == 0 &&
            !exposePythonExecutables()) {
            cerr << "\nPackage installed, but Crypt could not expose its CLI.\n";
            return 1;
        }
    }

    if (result == 0) {
        cout << "\n✓ "
             << package
             << " installed via "
             << backendName(backend)
             << ".\n";

        if (backend == Backend::NPM ||
            backend == Backend::PIP) {

            cout << "CLI directory: "
                 << cryptBinDir()
                 << "\n";
        }

        return 0;
    }

    cout << "\n✗ Installation failed.\n";

    return result;
}

int removePackage(const string& package,
                  Backend backend) {

    cout << "\nCrypt Package Manager\n";
    cout << "────────────────────────\n\n";

    cout << "Package   " << package << "\n";
    cout << "Source    "
         << backendName(backend)
         << "\n";

    if (!confirm()) {
        cout << "\nCancelled.\n";
        return 0;
    }

    cout << "\n:: Removing...\n\n";

    int result = 1;

    if (backend == Backend::DNF) {
        result = execute({
            "dnf",
            "remove",
            "-y",
            package
        });
    }

    else if (backend == Backend::NPM) {
        result = execute({
            "npm",
            "uninstall",
            "--global",
            "--prefix",
            npmPrefix().string(),
            package
        });

        exposeNpmExecutables();
    }

    else if (backend == Backend::PIP) {
        filesystem::path python =
            pipEnvironment() /
            "bin" /
            "python";

        if (!filesystem::exists(python)) {
            cerr << "Crypt Python environment does not exist.\n";
            return 1;
        }

        result = execute({
            python.string(),
            "-m",
            "pip",
            "uninstall",
            "-y",
            package
        });

        exposePythonExecutables();
    }

    if (result == 0) {
        cout << "\n✓ "
             << package
             << " removed.\n";

        return 0;
    }

    cout << "\n✗ Removal failed.\n";

    return result;
}

void searchPackage(const string& package,
                   Backend backend) {

    if (backend == Backend::DNF) {
        execute({
            "dnf",
            "search",
            package
        });

        return;
    }

    if (backend == Backend::NPM) {
        execute({
            "npm",
            "search",
            package
        });

        return;
    }

    if (backend == Backend::PIP) {
        execute({
            "python3",
            "-m",
            "pip",
            "index",
            "versions",
            package
        });

        return;
    }

    if (backend == Backend::ANY) {
        cout << "\nCrypt Package Manager\n";
        cout << "────────────────────────\n";

        Availability available =
            checkAvailability(package);

        /*
         * checkAvailability stops after Fedora
         * when a native exact match exists.
         */
        if (available.dnf) {
            cout << "\nPreferred source: Fedora\n";
            return;
        }

        if (!available.npm &&
            !available.pip) {
            cout << "\nNo exact match found.\n";
        }

        return;
    }
}

void infoPackage(const string& package,
                 Backend backend) {

    if (backend == Backend::ANY) {
        backend = resolveAny(package);

        if (backend == Backend::INVALID)
            return;
    }

    if (backend == Backend::DNF) {
        execute({
            "dnf",
            "info",
            package
        });

        return;
    }

    if (backend == Backend::NPM) {
        execute({
            "npm",
            "view",
            package
        });

        return;
    }

    if (backend == Backend::PIP) {
        execute({
            "python3",
            "-m",
            "pip",
            "index",
            "versions",
            package
        });
    }
}

Request parseRequest(int argc,
                     char* argv[],
                     int start) {

    Request request;

    if (argc <= start)
        return request;

    request.package = argv[start];

    if (!safeName(request.package))
        return request;

    request.backend = Backend::DNF;

    if (argc == start + 1) {
        request.valid = true;
        return request;
    }

    if (argc == start + 3 &&
        string(argv[start + 1]) == "w") {

        request.backend =
            parseBackend(argv[start + 2]);

        if (request.backend ==
            Backend::INVALID)
            return request;

        request.valid = true;
        return request;
    }

    return request;
}

void printHelp(bool shortMode) {
    cout << "\nCrypt Package Manager "
         << VERSION
         << "\n";

    cout << "────────────────────────\n\n";

    if (shortMode) {
        cout << "Usage\n";
        cout << "  cyt a <package> [w source]\n";
        cout << "  cyt r <package> [w source]\n";
        cout << "  cyt s <package> [w source]\n";
        cout << "  cyt i <package> [w source]\n";
        cout << "  cyt u\n";
        cout << "  cyt c\n\n";

        cout << "Commands\n";
        cout << "  a   add\n";
        cout << "  r   remove\n";
        cout << "  s   search\n";
        cout << "  i   info\n";
        cout << "  u   update\n";
        cout << "  c   clean\n";
        cout << "  h   help\n";
    }

    else {
        cout << "Usage\n";
        cout << "  crypt add <package> [w source]\n";
        cout << "  crypt remove <package> [w source]\n";
        cout << "  crypt search <package> [w source]\n";
        cout << "  crypt info <package> [w source]\n";
        cout << "  crypt update\n";
        cout << "  crypt clean\n";
    }

    cout << "\nSources\n";
    cout << "  Fedora   default / native\n";
    cout << "  w npm    npm registry\n";
    cout << "  w pip    Python / PyPI\n";
    cout << "  w any    automatically resolve\n";

    cout << "\nResolver policy\n";
    cout << "  Fedora exact match always wins.\n";
    cout << "  External ambiguity requires selection.\n";

    cout << "\nExamples\n";

    if (shortMode) {
        cout << "  cyt a neovim\n";
        cout << "  cyt a prettier w npm\n";
        cout << "  cyt a requests w pip\n";
        cout << "  cyt a neovim w any\n";
    }

    else {
        cout << "  crypt add neovim\n";
        cout << "  crypt add prettier w npm\n";
        cout << "  crypt add requests w pip\n";
        cout << "  crypt add neovim w any\n";
    }

    cout << "\nExternal CLI directory\n";
    cout << "  ~/.local/bin\n";
}

int main(int argc, char* argv[]) {
    string invoked =
        filesystem::path(argv[0])
            .filename()
            .string();

    bool shortMode =
        invoked == "cyt";

    if (argc == 1) {
        printHelp(shortMode);
        return 0;
    }

    string command = argv[1];

    if (command == "--version" ||
        command == "-v" ||
        command == "version") {

        cout << "Crypt "
             << VERSION
             << "\n";

        return 0;
    }

    if (shortMode) {
        if (command == "a") command = "add";
        else if (command == "r") command = "remove";
        else if (command == "s") command = "search";
        else if (command == "i") command = "info";
        else if (command == "u") command = "update";
        else if (command == "c") command = "clean";
        else if (command == "h") command = "help" ;

    if (command == "help" ||
        command == "--help" ||
        command == "-h") {

        printHelp(shortMode);
        return 0;
    }

    if (command == "update") {
        cout << "\n:: Updating Fedora packages...\n\n";

        int result = execute({
            "dnf",
            "upgrade",
            "-y"
        });

        if (commandExists("npm") &&
            filesystem::exists(npmPrefix())) {

            cout << "\n:: Updating Crypt npm packages...\n\n";

            int npmResult = execute({
                "npm",
                "update",
                "--global",
                "--prefix",
                npmPrefix().string()
            });

            if (npmResult != 0)
                result = npmResult;
        }

        filesystem::path python =
            pipEnvironment() /
            "bin" /
            "python";

        if (filesystem::exists(python)) {
            cout << "\n:: Updating pip...\n\n";

            execute({
                python.string(),
                "-m",
                "pip",
                "install",
                "--upgrade",
                "pip"
            });
        }

        return result;
    }

    if (command == "clean") {
        int result = execute({
            "dnf",
            "clean",
            "all"
        });

        if (commandExists("npm")) {
            execute({
                "npm",
                "cache",
                "clean",
                "--force"
            });
        }

        return result;
    }

    if (command != "add" &&
        command != "remove" &&
        command != "search" &&
        command != "info") {

        cerr << "Unknown command: "
             << command
             << "\n";

        cerr << "Run "
             << (shortMode ? "cyt h" : "crypt help")
             << " for usage.\n";

        return 1;
    }

    Request request =
        parseRequest(argc, argv, 2);

    if (!request.valid) {
        cerr << "Invalid package request.\n";
        cerr << "Run "
             << (shortMode ? "cyt h" : "crypt help")
             << " for usage.\n";

        return 1;
    }

    Backend backend =
        request.backend;

    if (backend == Backend::ANY &&
        (command == "add" ||
         command == "remove")) {

        if (command == "remove") {
            cerr << "Automatic source resolution for removal "
                    "is not available yet.\n";

            cerr << "Specify the source explicitly.\n";

            return 1;
        }

        backend =
            resolveAny(request.package);

        if (backend ==
            Backend::INVALID)
            return 1;
    }

    if (command == "add")
        return addPackage(
            request.package,
            backend
        );

    if (command == "remove")
        return removePackage(
            request.package,
            backend
        );

    if (command == "search") {
        searchPackage(
            request.package,
            backend
        );

        return 0;
    }

    if (command == "info") {
        infoPackage(
            request.package,
            backend
        );

        return 0;
    }

    return 0;
}
}
