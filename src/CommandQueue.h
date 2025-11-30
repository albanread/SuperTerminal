//
//  CommandQueue.h
//  SuperTerminal Framework - Thread-Safe Command Queue System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  This system allows Lua threads to safely call SuperTerminal APIs by
//  queuing commands to execute on the main thread.
//

#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <thread>
#include <future>
#include <memory>
#include <iostream>

namespace SuperTerminal {

// Base command interface
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual bool hasResult() const { return false; }
    virtual void* getResult() { return nullptr; }
};

// Command with return value
template<typename T>
class CommandWithResult : public Command {
private:
    std::function<T()> func;
    T result;
    bool executed = false;

public:
    CommandWithResult(std::function<T()> f) : func(f) {}
    
    void execute() override {
        result = func();
        executed = true;
    }
    
    bool hasResult() const override { return true; }
    
    void* getResult() override {
        if (!executed) return nullptr;
        return &result;
    }
    
    T getTypedResult() {
        return result;
    }
};

// Command without return value
class VoidCommand : public Command {
private:
    std::function<void()> func;

public:
    VoidCommand(std::function<void()> f) : func(f) {}
    
    void execute() override {
        func();
    }
};

// Main command queue class
class CommandQueue {
private:
    std::queue<std::shared_ptr<Command>> commands;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> shutdown_requested{false};
    std::atomic<bool> processing{false};

public:
    CommandQueue() = default;
    ~CommandQueue() { shutdown(); }

    // Disable copy and move
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue(CommandQueue&&) = delete;
    CommandQueue& operator=(CommandQueue&&) = delete;

    // Execute a command with return value (blocking)
    template<typename T>
    T executeCommand(std::function<T()> func) {
        if (shutdown_requested.load()) {
            throw std::runtime_error("CommandQueue is shutting down");
        }

        // If we're already on the main thread, execute directly
        if (isMainThread()) {
            return func();
        }

        auto command = std::make_shared<CommandWithResult<T>>(func);
        std::condition_variable cmd_cv;
        std::mutex cmd_mutex;
        bool completed = false;
        
        // Wrap the command to notify completion
        auto wrappedCommand = std::make_shared<VoidCommand>([command, &cmd_cv, &cmd_mutex, &completed]() {
            command->execute();
            {
                std::lock_guard<std::mutex> lock(cmd_mutex);
                completed = true;
            }
            cmd_cv.notify_one();
        });
        
        // Add to queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            commands.push(wrappedCommand);
        }
        queue_cv.notify_one();

        // Wait for execution
        std::unique_lock<std::mutex> lock(cmd_mutex);
        cmd_cv.wait(lock, [&completed]() { return completed; });

        return command->getTypedResult();
    }

    // Execute a void command (blocking)
    void executeVoidCommand(std::function<void()> func) {
        if (shutdown_requested.load()) {
            throw std::runtime_error("CommandQueue is shutting down");
        }

        // If we're already on the main thread, execute directly
        if (isMainThread()) {
            func();
            return;
        }

        std::condition_variable cmd_cv;
        std::mutex cmd_mutex;
        bool completed = false;
        
        auto command = std::make_shared<VoidCommand>([func, &cmd_cv, &cmd_mutex, &completed]() {
            func();
            {
                std::lock_guard<std::mutex> lock(cmd_mutex);
                completed = true;
            }
            cmd_cv.notify_one();
        });

        // Add to queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            commands.push(command);
        }
        queue_cv.notify_one();

        // Wait for completion
        std::unique_lock<std::mutex> lock(cmd_mutex);
        cmd_cv.wait(lock, [&completed]() { return completed; });
    }

    // Execute a void command with timeout (blocking with safety)
    bool executeVoidCommandWithTimeout(std::function<void()> func, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        if (shutdown_requested.load()) {
            throw std::runtime_error("CommandQueue is shutting down");
        }

        // If we're already on the main thread, execute directly
        if (isMainThread()) {
            func();
            return true;
        }

        std::condition_variable cmd_cv;
        std::mutex cmd_mutex;
        bool completed = false;
        
        auto command = std::make_shared<VoidCommand>([func, &cmd_cv, &cmd_mutex, &completed]() {
            func();
            {
                std::lock_guard<std::mutex> lock(cmd_mutex);
                completed = true;
            }
            cmd_cv.notify_one();
        });

        // Add to queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            commands.push(command);
        }
        queue_cv.notify_one();

        // Wait for completion with timeout
        std::unique_lock<std::mutex> lock(cmd_mutex);
        return cmd_cv.wait_for(lock, timeout, [&completed]() { return completed; });
    }

    // Execute a void command (non-blocking)
    void queueVoidCommand(std::function<void()> func) {
        if (shutdown_requested.load()) {
            return; // Silently ignore if shutting down
        }

        // If we're already on the main thread, execute directly
        if (isMainThread()) {
            func();
            return;
        }

        auto command = std::make_shared<VoidCommand>(func);

        // Add to queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            commands.push(command);
        }
        queue_cv.notify_one();
    }

    // Safe wrapper functions for common UI operations from Lua thread
    void safeSetWindowTitle(const std::string& title) {
        queueVoidCommand([title]() {
            extern void set_window_title(const char* title);
            set_window_title(title.c_str());
        });
    }

    void safeConsoleOutput(const std::string& message) {
        queueVoidCommand([message]() {
            std::cout << "[SuperTerminal Console] " << message << std::endl;
            std::cout.flush();
        });
    }

    void safePrint(const std::string& text) {
        queueVoidCommand([text]() {
            extern void print(const char* text);
            print(text.c_str());
        });
    }

    // Process commands (call from main thread)
    void processCommands() {
        processing = true;
        
        while (!shutdown_requested.load()) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            if (commands.empty()) {
                processing = false;
                return; // No commands to process
            }

            auto command = commands.front();
            commands.pop();
            lock.unlock();

            try {
                command->execute();
            } catch (const std::exception& e) {
                // Log error but continue processing
                std::cerr << "CommandQueue: Error executing command: " << e.what() << std::endl;
            }
        }
        
        processing = false;
    }

    // Process a single command (non-blocking)
    bool processSingleCommand() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        if (commands.empty() || shutdown_requested.load()) {
            return false;
        }

        auto command = commands.front();
        commands.pop();

        try {
            command->execute();
            queue_cv.notify_all(); // Notify waiting threads
            return true;
        } catch (const std::exception& e) {
            std::cerr << "CommandQueue: Error executing command: " << e.what() << std::endl;
            queue_cv.notify_all(); // Still notify to prevent deadlock
            return false;
        }
    }

    // Check if there are pending commands
    bool hasPendingCommands() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex));
        return !commands.empty();
    }

    // Get number of pending commands
    size_t getPendingCommandCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex));
        return commands.size();
    }

    // Shutdown the queue
    void shutdown() {
        shutdown_requested = true;
        queue_cv.notify_all();
        
        // Clear remaining commands
        std::lock_guard<std::mutex> lock(queue_mutex);
        while (!commands.empty()) {
            commands.pop();
        }
    }

    // Check if shutdown was requested
    bool isShuttingDown() const {
        return shutdown_requested.load();
    }

    // Check if currently processing
    bool isProcessing() const {
        return processing.load();
    }

private:
    // Check if we're on the main thread (uses external function)
    bool isMainThread() const;
};

// Global command queue instance
extern CommandQueue g_command_queue;

// Convenience macros for common API patterns
#define QUEUE_VOID_COMMAND(func_call) \
    g_command_queue.executeVoidCommand([=]() { func_call; })

#define QUEUE_RETURN_COMMAND(type, func_call) \
    g_command_queue.executeCommand<type>([=]() -> type { return func_call; })

// Initialize the command queue system
void command_queue_init();

// Shutdown the command queue system
void command_queue_shutdown();

// Process pending commands (call from main thread)
void command_queue_process();

// Process a single command (call from main thread render loop)
bool command_queue_process_single();

} // namespace SuperTerminal

#endif // COMMAND_QUEUE_H