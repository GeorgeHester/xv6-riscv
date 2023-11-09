/**
 * Custom shell for XV6
 */
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

/**
 * Define required constants
 */
#define NULL 0
#define BUFFER_SIZE 128
#define MAXIMUM_ARGUMENTS 16

char whitespace[6] = " \t\r\n\v";

/**
 * Function to handle fatal errors, prints a message to stderr and then exits
 * the shell
 */
void
error(char* message)
{
    fprintf(2, "%s\n", message);
    exit(1);
};

/**
 * Function to handle forking in a safe way so that it safely exits given a fork
 * failure
 */
int
safe_fork(void)
{
    // Get the process id
    int process_id = fork();

    // Check the fork was successful
    if (process_id == -1) error("Failed fork");

    return process_id;
};

/**
 * Custom implementation of the string.h function strrchr to get the last
 * pointer in a given string
 */
char*
strrchr(char* s, char c)
{
    char* p_s_end = s + strlen(s);

    for (; p_s_end >= s; p_s_end--)
        if (*p_s_end == c) return (char*)p_s_end;

    return 0;
};

enum CommandType
{
    execute_command,
    list_command,
    redirection_command,
    pipe_command
};

typedef struct Command
{
    enum CommandType type;
} Command;

typedef struct ExecuteCommand
{
    enum CommandType type;
    char* argv[MAXIMUM_ARGUMENTS];
} ExecuteCommand;

typedef struct ListCommand
{
    enum CommandType type;
    struct Command* p_left_command;
    struct Command* p_right_command;
} ListCommand;

typedef struct PipeCommand
{
    enum CommandType type;
    struct Command* p_left_command;
    struct Command* p_right_command;
} PipeCommand;

typedef struct RedirectionCommand
{
    enum CommandType type;
    struct Command* p_command;
    char* p_file_name;
    int file_mode;
    int file_descriptor;
} RedirectionCommand;

/**
 * Create and populate a redirection command structure
 */
ExecuteCommand*
create_execute_command(void)
{
    ExecuteCommand* command;

    command = malloc(sizeof(*command));
    memset(command, NULL, sizeof(*command));
    command->type = execute_command;
    return command;
};

/**
 * Create and populate a list command structure
 */
ListCommand*
create_list_command(Command* p_left_command, Command* p_right_command)
{
    ListCommand* command;

    command = malloc(sizeof(*command));
    memset(command, NULL, sizeof(*command));
    command->type = list_command;
    command->p_left_command = p_left_command;
    command->p_right_command = p_right_command;
    return command;
};

/**
 * Create and populate a pipe command structure
 */
PipeCommand*
create_pipe_command(Command* p_left_command, Command* p_right_command)
{
    PipeCommand* command;

    command = malloc(sizeof(*command));
    memset(command, NULL, sizeof(*command));
    command->type = pipe_command;
    command->p_left_command = p_left_command;
    command->p_right_command = p_right_command;
    return command;
};

/**
 * Create and populate a redirection command structure
 */
RedirectionCommand*
create_redirection_command(Command* p_command,
                           char* p_file_name,
                           int file_mode,
                           int file_descriptor)
{
    RedirectionCommand* command;

    command = malloc(sizeof(*command));
    memset(command, NULL, sizeof(*command));
    command->type = redirection_command;
    command->p_command = p_command;
    command->p_file_name = p_file_name;
    command->file_mode = file_mode;
    command->file_descriptor = file_descriptor;
    return command;
};

void
run_command(Command*) __attribute__((noreturn));

/**
 * Function to execute commands and any subordinate commands
 */
void
run_command(Command* command)
{
    int command_communication_pipe[2];

    ExecuteCommand* command_execute;
    ListCommand* command_list;
    PipeCommand* command_pipe;
    RedirectionCommand* command_redirection;

    // Check the command is not empty
    if (command == NULL) exit(1);

    switch (command->type)
    {
        default:
            // If command is an invalid type error out
            error("Failed run command");
            break;
        case execute_command:
            // Cast the command to the correct type
            command_execute = (ExecuteCommand*)command;

            // Check the command is not empty
            if (command_execute->argv[0] == NULL) exit(1);

            exec(command_execute->argv[0], command_execute->argv);
            fprintf(2, "Failed to execute %s\n", command_execute->argv[0]);
            break;
        case list_command:
            // Cast the command to the correct type
            command_list = (ListCommand*)command;

            // Execute the other parts of the command in order
            if (safe_fork() == 0) run_command(command_list->p_left_command);
            wait(0);

            run_command(command_list->p_right_command);
            break;
        case pipe_command:
            // Cast the command to the correct type
            command_pipe = (PipeCommand*)command;

            // Open a pipe and check it was successful
            if (pipe(command_communication_pipe) < 0)
                error("Failed to open pipe");

            // Fork for the first part of the command
            if (safe_fork() == 0)
            {
                close(1);
                dup(command_communication_pipe[1]);
                close(command_communication_pipe[0]);
                close(command_communication_pipe[1]);
                run_command(command_pipe->p_left_command);
            };

            // Fork for the second part of the command
            if (safe_fork() == 0)
            {
                close(0);
                dup(command_communication_pipe[0]);
                close(command_communication_pipe[0]);
                close(command_communication_pipe[1]);
                run_command(command_pipe->p_right_command);
            };

            // Close the pipe and wait for the parts of the command to finish
            // execution
            close(command_communication_pipe[0]);
            close(command_communication_pipe[1]);
            wait(0);
            wait(0);
            break;
        case redirection_command:
            // Cast the command to the correct type
            command_redirection = (RedirectionCommand*)command;

            // Close the file descriptor so the file takes that file descriptors
            // value
            close(command_redirection->file_descriptor);

            // Attempt to open the given file
            if (open(command_redirection->p_file_name,
                     command_redirection->file_mode) < 0)
            {
                fprintf(2,
                        "Failed to open file %s\n",
                        command_redirection->p_file_name);
                exit(1);
            };

            // Execute the subordinate command
            run_command(command_redirection->p_command);
            break;
    };

    exit(0);
};

Command*
parse_command(char* buffer);
Command*
parse_execute(char* buffer);
Command*
parse_list(char* buffer, char* p_split);
Command*
parse_pipe(char* buffer, char* p_split);
Command*
parse_redirection(char* buffer, char* p_split);
char*
parse_redirection_file_name(char* buffer);

Command*
parse_command(char* buffer)
{
    if (strchr(buffer, ';') != NULL)
    {
        Command* p_command;
        p_command = parse_list(buffer, strchr(buffer, ';'));
        return p_command;
    };

    if (strchr(buffer, '|') != NULL)
    {
        Command* p_command;
        p_command = parse_pipe(buffer, strchr(buffer, '|'));
        return p_command;
    };

    if (strrchr(buffer, '>') != NULL && strrchr(buffer, '<') == NULL)
    {
        Command* p_command;
        p_command = parse_redirection(buffer, strrchr(buffer, '>'));
        return p_command;
    };

    if (strrchr(buffer, '<') != NULL && strrchr(buffer, '>') == NULL)
    {
        Command* p_command;
        p_command = parse_redirection(buffer, strrchr(buffer, '<'));
        return p_command;
    };

    if (strrchr(buffer, '>') != NULL && strrchr(buffer, '<') != NULL)
    {
        char* p_redirection_right = strrchr(buffer, '>');
        char* p_redirection_left = strrchr(buffer, '<');

        if (p_redirection_right < p_redirection_left)
        {
            Command* p_command;
            p_command = parse_redirection(buffer, strrchr(buffer, '<'));
            return p_command;
        }
        else
        {
            Command* p_command;
            p_command = parse_redirection(buffer, strrchr(buffer, '>'));
            return p_command;
        };
    };

    Command* p_command;
    p_command = parse_execute(buffer);
    return p_command;
};

Command*
parse_execute(char* buffer)
{
    // Remove the whitespace from the start of the command
    while (strchr(whitespace, buffer[0]) != NULL)
        buffer++;

    // Create a new execute command
    ExecuteCommand* p_command = create_execute_command();

    // Check if the buffer is empty and return an empty command if so
    if (buffer[0] == NULL) return (Command*)p_command;

    // Set the default argument parameters
    int argi = 0;
    p_command->argv[argi] = buffer;

    // Loop through all the arguments in the command
    while (argi < MAXIMUM_ARGUMENTS)
    {
        // Check for end of the command reached
        if (buffer[0] == NULL) break;

        // Check if this is a split in the command
        if (strchr(whitespace, buffer[0]) != NULL)
        {
            // Set the end pointer to null for the argument and update to the
            // next argument index
            buffer[0] = NULL;
            buffer++;
            argi++;

            // Remove whitespace between arguments
            while (strchr(whitespace, buffer[0]) != NULL)
                buffer++;

            // Check for end of the command reached
            if (buffer[0] == NULL) break;

            // Set the start pointer for the next argument
            p_command->argv[argi] = buffer;
            continue;
        };

        // Move through the buffer
        buffer++;
    };

    return (Command*)p_command;
};

Command*
parse_list(char* buffer, char* p_split)
{
    // Set the token to null to terminate the command string
    buffer[p_split - buffer] = NULL;

    // Parse the left and right hand side of the token
    Command* p_left_command = parse_command(buffer);
    Command* p_right_command = parse_command(buffer + (p_split - buffer + 1));

    // Create a list command assigning the left and right hand commands
    ListCommand* p_command;
    p_command = create_list_command(p_left_command, p_right_command);

    return (Command*)p_command;
};

Command*
parse_pipe(char* buffer, char* p_split)
{
    // Set the token to null to terminate the command string
    buffer[p_split - buffer] = NULL;

    // Parse the left and right hand side of the token
    Command* p_left_command = parse_command(buffer);
    Command* p_right_command = parse_command(buffer + (p_split - buffer + 1));

    // Create a pipe command assigning the left and right hand commands
    PipeCommand* p_command;
    p_command = create_pipe_command(p_left_command, p_right_command);

    return (Command*)p_command;
};

Command*
parse_redirection(char* buffer, char* p_split)
{
    // Get the direction character then set the token to null to terminate the
    // command string
    char direction = buffer[p_split - buffer];
    buffer[p_split - buffer] = NULL;

    // Parse the command
    RedirectionCommand* p_command;
    Command* p_redirection_command = parse_command(buffer);

    // Get the file name string
    char* p_redirection_file_name = buffer + (p_split - buffer + 1);
    p_redirection_file_name =
      parse_redirection_file_name(p_redirection_file_name);

    // Create the redirection left command
    if (direction == '<')
    {
        p_command = create_redirection_command(
          p_redirection_command, p_redirection_file_name, O_RDONLY, 0);
        return (Command*)p_command;
    };

    // Create the redirection right command
    p_command = create_redirection_command(
      p_redirection_command, p_redirection_file_name, O_WRONLY | O_CREATE, 1);
    return (Command*)p_command;
};

char*
parse_redirection_file_name(char* buffer)
{
    // Remove the whitespace from the start of the file name
    while (strchr(whitespace, *buffer) != NULL)
        buffer++;

    char* output = buffer;

    while (strchr(whitespace, *buffer) == NULL && *buffer != NULL)
    {
        buffer++;
    };

    *buffer = NULL;
    if (output[0] == NULL) error("Failed to parse filename for redirection");

    return output;
};

/**
 * Open the file descriptors for the standard inputs and outputs
 */
void
open_console_file_descriptor(void)
{
    int file_descriptor;

    while ((file_descriptor = open("console", O_RDWR)) >= 0)
    {
        if (file_descriptor >= 3)
        {
            close(file_descriptor);
            break;
        };
    };
};

/**
 * Get input from user, outputting the required prompt
 */
int
prompt_user(char* buffer)
{
    // Output the user prompt
    write(1, ">>> ", 4);
    // Set the buffer to NULL
    memset(buffer, NULL, BUFFER_SIZE);

    // Read data from standard input up to the size of the buffer
    gets(buffer, BUFFER_SIZE);

    // Test to make sure the buffer is not empty
    if (buffer[0] == NULL) return -1;
    return 0;
};

/**
 * Remove the whitespace from before the buffer data starts
 */
void
remove_prefixed_whitespace_buffer(char* buffer)
{
    while (strchr(whitespace, buffer[0]) != NULL)
    {
        memcpy(buffer, buffer + 1, strlen(buffer));
    };
};

/**
 * Terminate the final character of the buffer
 */
void
terminate_buffer(char* buffer)
{
    buffer[strlen(buffer) - 1] = NULL;
};

int
main(void)
{
    static char buffer[BUFFER_SIZE];

    open_console_file_descriptor();

    while (prompt_user(buffer) >= 0)
    {
        // Process buffer so it can be parsed
        remove_prefixed_whitespace_buffer(buffer);
        terminate_buffer(buffer);

        // Copy the first part of the buffer to compare for the cd command
        char cd_test[4];
        memcpy(cd_test, &buffer, 3);
        cd_test[3] = NULL;

        // Compare the cd test string to test for a cd command
        if (strcmp(cd_test, "cd ") == 0)
        {
            // Process the directory variable
            char* directory = buffer + 3;
            remove_prefixed_whitespace_buffer(directory);

            // Execute the cd command
            int cd_success = chdir(directory);

            // Check for change directory success
            if (cd_success < 0) fprintf(2, "Failed cd %s\n", directory);
            continue;
        };

        // Fork to execute the command
        if (safe_fork() == 0) run_command(parse_command(buffer));
        wait(0);
    };

    exit(0);
};