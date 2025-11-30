//
//  superterminal_audiod.cpp
//  SuperTerminal - Audio Daemon Executable
//
//  Created by Assistant on 2024-11-25.
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AudioDaemon.h"
#include "AudioDaemonProtocol.h"
#include <iostream>
#include <string>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <syslog.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

using namespace SuperTerminal::AudioDaemon;

// Global daemon instance for signal handling
static std::unique_ptr<AudioDaemon> g_daemon;
static volatile sig_atomic_t g_shutdown_requested = 0;

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    switch (signal) {
        case SIGTERM:
        case SIGINT:
            std::cout << "Received shutdown signal (" << signal << "), stopping daemon...\n";
            g_shutdown_requested = 1;
            if (g_daemon) {
                g_daemon->stop();
            }
            break;
        case SIGHUP:
            std::cout << "Received SIGHUP, ignoring (no config reload implemented)\n";
            break;
        default:
            break;
    }
}

// Setup signal handlers
void setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    
    // Ignore SIGPIPE (broken socket connections)
    signal(SIGPIPE, SIG_IGN);
}

// Daemonize process
bool daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Failed to fork daemon process");
        return false;
    }
    
    if (pid > 0) {
        // Parent process exits
        exit(0);
    }
    
    // Child process continues
    if (setsid() < 0) {
        perror("Failed to create new session");
        return false;
    }
    
    // Fork again to prevent acquiring controlling terminal
    pid = fork();
    if (pid < 0) {
        perror("Failed to fork second time");
        return false;
    }
    
    if (pid > 0) {
        // First child exits
        exit(0);
    }
    
    // Change working directory to root
    if (chdir("/") < 0) {
        perror("Failed to change working directory");
        return false;
    }
    
    // Close file descriptors
    for (int i = sysconf(_SC_OPEN_MAX); i >= 0; i--) {
        close(i);
    }
    
    // Redirect stdin, stdout, stderr to /dev/null
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd >= 0) {
        dup2(nullfd, STDIN_FILENO);
        dup2(nullfd, STDOUT_FILENO);
        dup2(nullfd, STDERR_FILENO);
        if (nullfd > STDERR_FILENO) {
            close(nullfd);
        }
    }
    
    return true;
}

// Write PID file
bool writePidFile(const std::string& pidFile) {
    FILE* fp = fopen(pidFile.c_str(), "w");
    if (!fp) {
        perror("Failed to create PID file");
        return false;
    }
    
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    return true;
}

// Remove PID file
void removePidFile(const std::string& pidFile) {
    unlink(pidFile.c_str());
}

// Print usage information
void printUsage(const char* programName) {
    std::cout << "SuperTerminal Audio Daemon v1.0\n\n";
    std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  -s, --socket PATH       Unix socket path (default: " << DEFAULT_SOCKET_PATH << ")\n";
    std::cout << "  -d, --daemon            Run as daemon (background process)\n";
    std::cout << "  -p, --pid-file PATH     PID file path (only with --daemon)\n";
    std::cout << "  -c, --max-clients NUM   Maximum number of clients (default: 10)\n";
    std::cout << "  -q, --max-queue NUM     Maximum queue size (default: 100)\n";
    std::cout << "  -v, --verbose           Verbose logging\n";
    std::cout << "  -f, --foreground        Run in foreground (don't daemonize)\n";
    std::cout << "  --version               Show version information\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << "                    # Run in foreground with defaults\n";
    std::cout << "  " << programName << " --daemon           # Run as background daemon\n";
    std::cout << "  " << programName << " -s /tmp/my.sock    # Use custom socket path\n";
    std::cout << "  " << programName << " -d -p /var/run/audiod.pid  # Daemon with PID file\n";
    std::cout << "\nSignals:\n";
    std::cout << "  SIGTERM, SIGINT         Graceful shutdown\n";
    std::cout << "  SIGHUP                  Reload configuration (not implemented)\n";
}

// Print version information
void printVersion() {
    std::cout << "SuperTerminal Audio Daemon v1.0\n";
    std::cout << "Protocol version: " << PROTOCOL_VERSION << "\n";
    std::cout << "Built on: " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "Copyright © 2024 SuperTerminal Project\n";
}

// Check if daemon is already running
bool isDaemonRunning(const std::string& socketPath) {
    return AudioDaemonLauncher::isDaemonRunning(socketPath);
}

// Main daemon loop
int runDaemon(const std::string& socketPath, size_t maxClients, size_t maxQueue, bool verbose) {
    try {
        // Create and configure daemon
        g_daemon = std::make_unique<AudioDaemon>(socketPath);
        g_daemon->setMaxClients(maxClients);
        g_daemon->setQueueSize(maxQueue);
        
        // Start the daemon
        if (!g_daemon->start()) {
            std::cerr << "Failed to start audio daemon\n";
            return 1;
        }
        
        if (verbose) {
            std::cout << "Audio daemon started successfully\n";
            std::cout << "Socket path: " << socketPath << "\n";
            std::cout << "Max clients: " << maxClients << "\n";
            std::cout << "Max queue size: " << maxQueue << "\n";
            std::cout << "PID: " << getpid() << "\n";
        }
        
        // Main daemon loop - wait for shutdown signal
        while (!g_shutdown_requested && g_daemon->isRunning()) {
            sleep(1);
            
            if (verbose) {
                // Print periodic status
                static int statusCounter = 0;
                if (++statusCounter >= 30) { // Every 30 seconds
                    std::cout << "Status - Clients: " << g_daemon->getClientCount() 
                             << ", Queue: " << g_daemon->getQueueSize() 
                             << ", State: " << static_cast<int>(g_daemon->getCurrentState()) << "\n";
                    statusCounter = 0;
                }
            }
        }
        
        if (verbose) {
            std::cout << "Shutting down daemon...\n";
        }
        
        // Stop the daemon
        g_daemon->stop();
        g_daemon.reset();
        
        if (verbose) {
            std::cout << "Daemon stopped successfully\n";
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Daemon error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown daemon error\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    // Default configuration
    std::string socketPath = DEFAULT_SOCKET_PATH;
    std::string pidFile;
    size_t maxClients = 10;
    size_t maxQueue = 100;
    bool daemonMode = false;
    bool verbose = false;
    bool foreground = false;
    
    // Parse command line options
    static struct option longOptions[] = {
        {"help", no_argument, 0, 'h'},
        {"socket", required_argument, 0, 's'},
        {"daemon", no_argument, 0, 'd'},
        {"pid-file", required_argument, 0, 'p'},
        {"max-clients", required_argument, 0, 'c'},
        {"max-queue", required_argument, 0, 'q'},
        {"verbose", no_argument, 0, 'v'},
        {"foreground", no_argument, 0, 'f'},
        {"version", no_argument, 0, 1000},
        {0, 0, 0, 0}
    };
    
    int opt;
    int optionIndex = 0;
    
    while ((opt = getopt_long(argc, argv, "hs:dp:c:q:vf", longOptions, &optionIndex)) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                return 0;
                
            case 's':
                socketPath = optarg;
                break;
                
            case 'd':
                daemonMode = true;
                break;
                
            case 'p':
                pidFile = optarg;
                break;
                
            case 'c':
                maxClients = std::max(1, std::min(100, atoi(optarg)));
                break;
                
            case 'q':
                maxQueue = std::max(1, std::min(1000, atoi(optarg)));
                break;
                
            case 'v':
                verbose = true;
                break;
                
            case 'f':
                foreground = true;
                break;
                
            case 1000: // --version
                printVersion();
                return 0;
                
            case '?':
                std::cerr << "Use --help for usage information\n";
                return 1;
                
            default:
                break;
        }
    }
    
    // Validate arguments
    if (daemonMode && foreground) {
        std::cerr << "Error: Cannot specify both --daemon and --foreground\n";
        return 1;
    }
    
    if (!pidFile.empty() && !daemonMode) {
        std::cerr << "Warning: PID file only used in daemon mode\n";
    }
    
    // Check if daemon is already running
    if (isDaemonRunning(socketPath)) {
        std::cerr << "Error: Audio daemon is already running on " << socketPath << "\n";
        std::cerr << "Use 'killall superterminal_audiod' to stop it first\n";
        return 1;
    }
    
    // Setup signal handlers
    setupSignalHandlers();
    
    // Initialize logging
    if (daemonMode && !foreground) {
        openlog("superterminal_audiod", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        if (verbose) {
            syslog(LOG_INFO, "Starting SuperTerminal Audio Daemon");
        }
    }
    
    // Daemonize if requested
    if (daemonMode && !foreground) {
        if (verbose) {
            std::cout << "Daemonizing process...\n";
        }
        
        if (!daemonize()) {
            std::cerr << "Failed to daemonize process\n";
            return 1;
        }
        
        // Write PID file if specified
        if (!pidFile.empty()) {
            if (!writePidFile(pidFile)) {
                syslog(LOG_ERR, "Failed to write PID file: %s", pidFile.c_str());
                return 1;
            }
        }
    }
    
    // Run the daemon
    int result = runDaemon(socketPath, maxClients, maxQueue, verbose);
    
    // Cleanup
    if (!pidFile.empty()) {
        removePidFile(pidFile);
    }
    
    if (daemonMode && !foreground) {
        if (verbose) {
            syslog(LOG_INFO, "SuperTerminal Audio Daemon stopped");
        }
        closelog();
    }
    
    return result;
}