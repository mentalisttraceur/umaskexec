/* SPDX-License-Identifier: 0BSD */
/* Copyright 2019 Alexander Kozhevnikov <mentalisttraceur@gmail.com> */

/* Standard C library headers */
#include <errno.h> /* errno */
#include <stdio.h> /* EOF, fputc, fputs, perror, stderr, stdout */
#include <stdlib.h> /* EXIT_FAILURE, EXIT_SUCCESS */
#include <string.h> /* strcmp */

/* Standard UNIX/Linux (POSIX/SUS base) headers */
#include <sys/stat.h> /* umask */
#include <sys/types.h> /* mode_t */
#include <unistd.h> /* execvp */


char const version_text[] = "umaskexec 1.0.0\n";

char const help_text[] =
    "Execute a command with the given file mode creation mask.\n"
    "If no mask is given, show the current mask.\n"
    "If no command is given, show what mask would be used.\n"
    "\n"
    "Usage:\n"
    "    umaskexec [--symbolic | --] [<mask> [<command> [<argument>]...]]\n"
    "    umaskexec (--help | --version) [<ignored>]...\n"
    "\n"
    "Options:\n"
    "    -h --help      show this help text\n"
    "    -V --version   show version information\n"
    "    -S --symbolic  show the mask symbolically instead of in octal\n"
;


static
int error_bad_option(char * option, char * arg0)
{
    if(fputs(arg0, stderr) != EOF
    && fputs(": bad option: ", stderr) != EOF
    && fputs(option, stderr) != EOF)
    {
        fputc('\n', stderr);
    }
    return EXIT_FAILURE;
}


static
int error_writing_output(char * arg0)
{
    int errno_ = errno;
    if(fputs(arg0, stderr) != EOF)
    {
        errno = errno_;
        perror(": error writing output");
    }
    return EXIT_FAILURE;
}


static
int error_bad_umask(char * umask_string, char * arg0)
{
    if(fputs(arg0, stderr) != EOF
    && fputs(": bad umask: ", stderr) != EOF
    && fputs(umask_string, stderr) != EOF)
    {
        fputc('\n', stderr);
    }
    return EXIT_FAILURE;
}


static
int error_executing_command(char * command, char * arg0)
{
    int errno_ = errno;
    if(fputs(arg0, stderr) != EOF
    && fputs(": error executing command: ", stderr) != EOF)
    {
        errno = errno_;
        perror(command);
    }
    return EXIT_FAILURE;
}


static
int print_help(char * arg0)
{
    if(fputs(help_text, stdout) == EOF
    || fflush(stdout) == EOF)
    {
        return error_writing_output(arg0);
    }
    return EXIT_SUCCESS;
}


static
int print_version(char * arg0)
{
    if(fputs(version_text, stdout) == EOF
    || fflush(stdout) == EOF)
    {
        return error_writing_output(arg0);
    }
    return EXIT_SUCCESS;
}


static
int print_umask_octal(char * arg0)
{
    char mask_string[] = "0000\n";
    mode_t mask = umask(0);
    mask_string[1] += 7 & (mask >> 6);
    mask_string[2] += 7 & (mask >> 3);
    mask_string[3] += 7 & (mask >> 0);

    if(fputs(mask_string, stdout) == EOF
    || fflush(stdout) == EOF)
    {
        return error_writing_output(arg0);
    }
    return EXIT_SUCCESS;
}


static
int print_umask_symbolic(char * arg0)
{
    char mask_string[sizeof("u=rwx,g=rwx,o=rwx\n")];
    char * next_character = mask_string;
    mode_t mask = umask(0);

    *next_character++ = 'u';
    *next_character++ = '=';
    if(!(mask & S_IRUSR)) *next_character++ = 'r';
    if(!(mask & S_IWUSR)) *next_character++ = 'w';
    if(!(mask & S_IXUSR)) *next_character++ = 'x';
    *next_character++ = ',';
    *next_character++ = 'g';
    *next_character++ = '=';
    if(!(mask & S_IRGRP)) *next_character++ = 'r';
    if(!(mask & S_IWGRP)) *next_character++ = 'w';
    if(!(mask & S_IXGRP)) *next_character++ = 'x';
    *next_character++ = ',';
    *next_character++ = 'o';
    *next_character++ = '=';
    if(!(mask & S_IROTH)) *next_character++ = 'r';
    if(!(mask & S_IWOTH)) *next_character++ = 'w';
    if(!(mask & S_IXOTH)) *next_character++ = 'x';
    *next_character++ = '\n';
    *next_character = '\0';

    if(fputs(mask_string, stdout) == EOF
    || fflush(stdout) == EOF)
    {
        return error_writing_output(arg0);
    }
    return EXIT_SUCCESS;
}


static
int parse_and_set_umask_octal(char * mask_string)
{
    mode_t new_mask = 0;
    char character = *mask_string;
    do
    {
        if(character < '0' || character > '7')
        {
            return 0;
        }
        new_mask <<= 3;
        new_mask += character - '0';
        if(new_mask > 0777)
        {
            return 0;
        }
        mask_string += 1;
        character = *mask_string;
    }
    while(character != '\0');
    umask(new_mask);
    return 1;
}


static
int parse_and_set_umask_symbolic(char * mask_string)
{
    /* umask bits combined by permission type: */
    static mode_t const r_bits = S_IRUSR | S_IRGRP | S_IROTH;
    static mode_t const w_bits = S_IWUSR | S_IWGRP | S_IWOTH;
    static mode_t const x_bits = S_IXUSR | S_IXGRP | S_IXOTH;

    /* umask bits combined by who they apply to: */
    static mode_t const u_bits = S_IRUSR | S_IWUSR | S_IXUSR;
    static mode_t const g_bits = S_IRGRP | S_IWGRP | S_IXGRP;
    static mode_t const o_bits = S_IROTH | S_IWOTH | S_IXOTH;
    static mode_t const a_bits = u_bits | g_bits | o_bits;

    /* new umask starts as the old umask, then is updated: */
    mode_t new_mask = umask(0);

    for(;;)
    {
        mode_t u_g_o_a_target = 0;
        char character;

        for(;;)
        {
            character = *mask_string++;
            if(character == 'u') u_g_o_a_target |= u_bits;
            else
            if(character == 'g') u_g_o_a_target |= g_bits;
            else
            if(character == 'o') u_g_o_a_target |= o_bits;
            else
            if(character == 'a') u_g_o_a_target |= a_bits;
            else
            break;
        }

        if(!u_g_o_a_target)
        {
            u_g_o_a_target = a_bits;
        }

        do
        {
            int inverted = 0;

            switch(character)
            {
                case '=':
                    new_mask |= u_g_o_a_target;
                    /* '=' is the same as '+' after setting the bits */
                case '+':
                    new_mask = ~new_mask;
                    inverted = 1;
                    /* '+' is the same as '-' once the mask is inverted */
                case '-':
                    break;
                default:
                    return 0;
            }

            for(;;)
            {
                character = *mask_string++;

                /* Symbolic umask '-' sets bits in binary umask. '+' and '=' */
                /* clear bits, which is setting bits on the inverted mask. */

                if(character == 'r') new_mask |= r_bits & u_g_o_a_target;
                else
                if(character == 'w') new_mask |= w_bits & u_g_o_a_target;
                else
                if(character == 'x') new_mask |= x_bits & u_g_o_a_target;
                else
                break;
            }

            if(inverted)
            {
                new_mask = ~new_mask;
            }

            if(character == '\0')
            {
                umask(new_mask);
                return 1;
            }
        }
        while(character != ',');
    }
}


static
int parse_and_set_umask(char * mask_string)
{
    if(parse_and_set_umask_octal(mask_string)
    || parse_and_set_umask_symbolic(mask_string))
    {
        return 1;
    }
    return 0;
}


int main(int argc, char * * argv)
{
    char * arg;
    char * arg0 = *argv;

    /* Function pointer holds octal or symbolic umask printing choice: */
    int (* print_umask)(char * arg0) = print_umask_octal;

    /* Without any arguments (two, counting argv[0]), just print the umask: */
    if(argc < 2)
    {
        /* Many systems allow execution without even the zeroth argument: */
        if(!arg0)
        {
            arg0 = "";
        }
        return print_umask(arg0);
    }

    /* The goal is to shift argv until it points to the command to execute: */
    argv += 1;

    /* First argument is either an option (starts with '-') or the umask: */
    arg = *argv;

    if(*arg == '-')
    {
        arg += 1;
        if(!strcmp(arg, "-help") || !strcmp(arg, "h"))
        {
            return print_help(arg0);
        }
        if(!strcmp(arg, "-version") || !strcmp(arg, "V"))
        {
            return print_version(arg0);
        }

        if(!strcmp(arg, "-symbolic") || !strcmp(arg, "S"))
        {
            print_umask = print_umask_symbolic;
        }
        else
        /* If it is *not* the "end of options" ("--") "option": */
        if(strcmp(arg, "-"))
        {
            return error_bad_option(arg - 1, arg0);
        }

        /* Shift argv past the consumed option, leaving the umask: */
        argv += 1;
        arg = *argv;

        /* No more arguments after parsing options? Print the umask: */
        if(!arg)
        {
            return print_umask(arg0);
        }
    }

    /* Now arg should be the umask. */

    if(!parse_and_set_umask(arg))
    {
        return error_bad_umask(arg, arg0);
    }

    /* Shift argv past the umask, leaving just the command in argv: */
    argv += 1;

    arg = *argv;
    /* Now arg should be the command to execute. */

    if(!arg)
    {
        /* If no command was given, just print the new umask: */
        return print_umask(arg0);
    }

    execvp(arg, argv);
    /* If we're here, execvp failed to execute the command. */

    return error_executing_command(arg, arg0);
}
