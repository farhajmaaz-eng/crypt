#include <array>
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

constexpr const char* VERSION = "0.5.0";

enum class Backend {
    DNF,
    NPM,
    PIP,
    ANY,
    INVALID
};

// ---------------------------------------------------------
// Basic utilities
// ---------------------------------------------------------

void banner() {
    std::cout
        << "\n"
        << "  Crypt Package Manager\n"
        << "  ─────────────────────────────\n";
}

std::string backendName(Backend b) {
    switch (b) {
        case Backend::DNF: return "Fedora";
        case Backend::NPM: return "npm";
        case Backend::PIP: return "pip";
        case Backend::ANY: return "any";
        default: return "unknown";
    }
}

Backend parseBackend(const std::string& name) {
    if (name == "dnf" || name == "fedora")
        return Backend::DNF;

    if (name == "npm")
        return Backend::NPM;

    if (name == "pip" || name == "pip3" || name == "pypi")
        return Backend::PIP;

    if (name == "any")
        return Backend::ANY;

    return Backend::INVALID;
}

bool safeName(const std::string& text) {
    if (text.empty())
        return false;

    for (unsigned char c : text) {
        if (!(std::isalnum(c) ||
              c == '-' ||
              c == '_' ||
              c == '.' ||
              c == '+' ||
              c == ':' ||
              c == '@' ||
              c == '/')) {
            return false;
        }
    }

    return true;
}

int execute(const std::vector<std::string>& args) {
    if (args.empty())
        return 1;

    std::vector<char*> argv;

    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));

    argv.push_back(nullptr);

    pid_t pid = fork();

    if (pid == 0) {
        execvp(argv[0], argv.data());
        perror("crypt");
        _exit(127);
    }

    if (pid < 0) {
        perror("crypt");
        return 1;
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        perror("crypt");
        return 1;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return 1;
}

std::string capture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");

    if (!pipe)
        return "";

    while (fgets(buffer.data(), buffer.size(), pipe))
        output += buffer.data();

    pclose(pipe);

    return output;
}

// ---------------------------------------------------------
// Package source parsing
//
// cyt a neovim
// cyt a prettier w npm
// cyt a requests w pip
// cyt a neovim w any
// ---------------------------------------------------------

struct Request {
    std::string package;
    Backend backend = Backend::DNF;
    bool valid = false;
};

Request parseRequest(
    const std::vector<std::string>& args
) {
    Request result;

    if (args.empty())
        return result;

    result.package = args[0];

    if (!safeName(result.package))
        return result;

    // No "w" = Fedora
    if (args.size() == 1) {
        result.backend = Backend::DNF;
        result.valid = true;
        return result;
    }

    if (args.size() == 3 && args[1] == "w") {
        result.backend = parseBackend(args[2]);

        if (result.backend != Backend::INVALID)
            result.valid = true;

        return result;
    }

    return result;
}

// ---------------------------------------------------------
// Resolver
// ---------------------------------------------------------

bool dnfExists(const std::string& package) {
    if (!safeName(package))
        return false;

    std::string command =
        "dnf repoquery --latest-limit 1 --qf '%{name}' "
        + package +
        " 2>/dev/null";

    std::string result = capture(command);

    std::stringstream lines(result);
    std::string line;

    while (std::getline(lines, line)) {
        if (line == package)
            return true;
    }

    return false;
}

bool npmExists(const std::string& package) {
    if (!safeName(package))
        return false;

    std::string command =
        "npm view " + package +
        " name --silent 2>/dev/null";

    std::string result = capture(command);

    while (!result.empty() &&
           (result.back() == '\n' ||
            result.back() == '\r')) {
        result.pop_back();
    }

    return !result.empty();
}

bool pipExists(const std::string& package) {
    if (!safeName(package))
        return false;

    // Query PyPI through pip's index command.
    std::string command =
        "python3 -m pip index versions "
        + package +
        " 2>/dev/null";

    std::string result = capture(command);

    return !result.empty();
}

Backend resolveAny(const std::string& package) {
    banner();

    std::cout
        << "\n"
        << "  Resolving " << package << "\n"
        << "\n";

    // HARD RULE: Fedora always gets first refusal.

    std::cout << "  Fedora   ";

    if (dnfExists(package)) {
        std::cout << "✓ available\n";
        std::cout
            << "\n"
            << "  Selected Fedora\n"
            << "  Native package priority.\n\n";

        return Backend::DNF;
    }

    std::cout << "not found\n";

    std::cout << "  npm      ";

    bool npm = npmExists(package);

    if (npm)
        std::cout << "✓ available\n";
    else
        std::cout << "not found\n";

    std::cout << "  PyPI     ";

    bool pip = pipExists(package);

    if (pip)
        std::cout << "✓ available\n";
    else
        std::cout << "not found\n";

    std::cout << "\n";

    // For v0.5:
    // npm gets priority over pip when Fedora has no match.
    // We'll make this smarter later.

    if (npm) {
        std::cout
            << "  Selected npm\n\n";
        return Backend::NPM;
    }

    if (pip) {
        std::cout
            << "  Selected pip\n\n";
        return Backend::PIP;
    }

    return Backend::INVALID;
}

// ---------------------------------------------------------
// ADD
// ---------------------------------------------------------

int addPackage(
    const std::string& package,
    Backend backend
) {
    if (backend == Backend::ANY) {
        backend = resolveAny(package);

        if (backend == Backend::INVALID) {
            std::cerr
                << "crypt: package '"
                << package
                << "' wasn't found in any source\n";

            return 1;
        }
    }

    banner();

    std::cout
        << "\n"
        << "  Package   " << package << "\n"
        << "  Source    " << backendName(backend) << "\n"
        << "\n"
        << "  :: Installing...\n\n";

    int result = 1;

    if (backend == Backend::DNF) {
        result = execute({
            "dnf",
            "install",
            package
        });
    }

    else if (backend == Backend::NPM) {
        result = execute({
            "npm",
            "install",
            "-g",
            package
        });
    }

    else if (backend == Backend::PIP) {
        result = execute({
            "python3",
            "-m",
            "pip",
            "install",
            "--user",
            package
        });
    }

    if (result == 0) {
        std::cout
            << "\n"
            << "  ✓ "
            << package
            << " installed via "
            << backendName(backend)
            << ".\n\n";
    }

    return result;
}

// ---------------------------------------------------------
// REMOVE
// ---------------------------------------------------------

int removePackage(
    const std::string& package,
    Backend backend
) {
    if (backend == Backend::ANY) {
        std::cerr
            << "crypt: 'w any' is currently only "
            << "supported for add/search\n";
        return 1;
    }

    banner();

    std::cout
        << "\n"
        << "  Removing " << package
        << " via " << backendName(backend)
        << "\n\n";

    int result = 1;

    if (backend == Backend::DNF) {
        result = execute({
            "dnf",
            "remove",
            package
        });
    }

    else if (backend == Backend::NPM) {
        result = execute({
            "npm",
            "uninstall",
            "-g",
            package
        });
    }

    else if (backend == Backend::PIP) {
        result = execute({
            "python3",
            "-m",
            "pip",
            "uninstall",
            package
        });
    }

    if (result == 0) {
        std::cout
            << "\n"
            << "  ✓ Removal complete.\n\n";
    }

    return result;
}

// ---------------------------------------------------------
// SEARCH
// ---------------------------------------------------------

int searchPackage(
    const std::string& package,
    Backend backend
) {
    banner();

    std::cout
        << "\n"
        << "  Search: " << package << "\n\n";

    if (backend == Backend::DNF) {
        return execute({
            "dnf",
            "search",
            package
        });
    }

    if (backend == Backend::NPM) {
        return execute({
            "npm",
            "search",
            package
        });
    }

    if (backend == Backend::PIP) {
        std::cout
            << "  PyPI exact package lookup\n\n";

        return execute({
            "python3",
            "-m",
            "pip",
            "index",
            "versions",
            package
        });
    }

    if (backend == Backend::ANY) {
        std::cout << "  Fedora   "
                  << (dnfExists(package)
                      ? "✓ available"
                      : "not found")
                  << "\n";

        std::cout << "  npm      "
                  << (npmExists(package)
                      ? "✓ available"
                      : "not found")
                  << "\n";

        std::cout << "  PyPI     "
                  << (pipExists(package)
                      ? "✓ available"
                      : "not found")
                  << "\n\n";

        return 0;
    }

    return 1;
}

// ---------------------------------------------------------
// INFO
// ---------------------------------------------------------

int infoPackage(
    const std::string& package,
    Backend backend
) {
    if (backend == Backend::DNF) {
        return execute({
            "dnf",
            "info",
            package
        });
    }

    if (backend == Backend::NPM) {
        return execute({
            "npm",
            "view",
            package
        });
    }

    if (backend == Backend::PIP) {
        return execute({
            "python3",
            "-m",
            "pip",
            "index",
            "versions",
            package
        });
    }

    if (backend == Backend::ANY) {
        Backend resolved = resolveAny(package);

        if (resolved == Backend::INVALID) {
            std::cerr
                << "crypt: package not found\n";
            return 1;
        }

        return infoPackage(package, resolved);
    }

    return 1;
}

// ---------------------------------------------------------
// HELP
// ---------------------------------------------------------

void help(bool shortMode) {
    banner();

    std::cout
        << "\n"
        << "  Crypt " << VERSION << "\n\n";

    if (shortMode) {
        std::cout
            << "  cyt a <package>              Add\n"
            << "  cyt r <package>              Remove\n"
            << "  cyt s <package>              Search\n"
            << "  cyt i <package>              Info\n"
            << "  cyt u                        Update\n"
            << "  cyt c                        Clean\n";
    } else {
        std::cout
            << "  crypt add <package>\n"
            << "  crypt remove <package>\n"
            << "  crypt search <package>\n"
            << "  crypt info <package>\n"
            << "  crypt update\n"
            << "  crypt clean\n";
    }

    std::cout
        << "\n"
        << "  Sources\n"
        << "  ─────────────────────────────\n"
        << "  w npm      npm registry\n"
        << "  w pip      Python / PyPI\n"
        << "  w any      Automatically resolve\n"
        << "\n"
        << "  Examples\n"
        << "  ─────────────────────────────\n"
        << "  cyt a neovim\n"
        << "  cyt a prettier w npm\n"
        << "  cyt a requests w pip\n"
        << "  cyt a neovim w any\n"
        << "\n";
}

// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

int main(int argc, char* argv[]) {
    std::string invokedAs =
        std::filesystem::path(argv[0]).filename();

    bool shortMode = (invokedAs == "cyt");

    if (argc < 2) {
        help(shortMode);
        return 0;
    }

    std::string action = argv[1];

    // Aliases
    if (action == "a") action = "add";
    if (action == "r") action = "remove";
    if (action == "s") action = "search";
    if (action == "i") action = "info";
    if (action == "u") action = "update";
    if (action == "c") action = "clean";
    if (action == "h") action = "help";

    if (action == "-v" ||
        action == "--version") {
        std::cout
            << "Crypt "
            << VERSION
            << "\n";
        return 0;
    }

    if (action == "help" ||
        action == "-h" ||
        action == "--help") {
        help(shortMode);
        return 0;
    }

    if (action == "update") {
        banner();

        std::cout
            << "\n"
            << "  :: Updating Fedora packages...\n\n";

        int dnf = execute({
            "dnf",
            "upgrade"
        });

        std::cout
            << "\n"
            << "  :: Updating global npm packages...\n\n";

        int npm = execute({
            "npm",
            "update",
            "-g"
        });

        return (dnf == 0 && npm == 0) ? 0 : 1;
    }

    if (action == "clean") {
        banner();

        execute({
            "dnf",
            "clean",
            "all"
        });

        execute({
            "npm",
            "cache",
            "clean",
            "--force"
        });

        std::cout
            << "\n"
            << "  ✓ Cleanup complete.\n\n";

        return 0;
    }

    std::vector<std::string> args;

    for (int i = 2; i < argc; ++i)
        args.push_back(argv[i]);

    Request request = parseRequest(args);

    if (!request.valid) {
        std::cerr
            << "crypt: invalid syntax\n"
            << "Try 'cyt help'.\n";

        return 1;
    }

    if (action == "add")
        return addPackage(
            request.package,
            request.backend
        );

    if (action == "remove")
        return removePackage(
            request.package,
            request.backend
        );

    if (action == "search")
        return searchPackage(
            request.package,
            request.backend
        );

    if (action == "info")
        return infoPackage(
            request.package,
            request.backend
        );

    std::cerr
        << "crypt: unknown command '"
        << action
        << "'\n";

    return 1;
}
