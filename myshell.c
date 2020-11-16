#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_BACKGROUND    "\x1b[48;2;60;80;150m"
#define ANSI_ERROR_BG      "\x1b[48;2;150;00;30m"
#define ANSI_BALD          "\x1b[1m"

void shellPerror(char *str)
{
    printf(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
    printf(": %s\n", str);
}

char *basename(char *path)
{
    char *s = strrchr(path, '/');
    return strdup(s + 1);
}

int askInput()
{
    char cwd[PATH_MAX], usr[HOST_NAME_MAX];
    
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        return 1;
    }
    if (getlogin_r(usr, sizeof(usr)))
    {
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        return 1;
    }
    
    char *base = basename(cwd);
    if (strcmp(cwd, "/")) strcpy(cwd, base);
    if (!strcmp(cwd, usr)) strcpy(cwd, "~");
    
    printf(ANSI_BACKGROUND ANSI_BALD);
    printf("%s %s" ANSI_COLOR_RESET " ", usr, cwd);
    
    free(base);
    return 0;
}

typedef enum {
    META_NULL,
    META_EXEC,
    META_PIPE,
    META_OR_EXEC,
    META_AND_EXEC,
    META_BACKGROUND,
    META_GROUP_START,
    META_GROUP_END,
    
    META_EXPECT_FILE,
    META_WRITE,
    META_READ,
    META_APPEND,
} Meta;

Meta determMeta(char *metaToken)
{
    if (!metaToken) return META_NULL;
    else if (!strcmp(metaToken, ";")) return META_EXEC;
    else if (!strcmp(metaToken, "|")) return META_PIPE;
    else if (!strcmp(metaToken, "||")) return META_OR_EXEC;
    else if (!strcmp(metaToken, "&&")) return META_AND_EXEC;
    else if (!strcmp(metaToken, "&")) return META_BACKGROUND;
    else if (!strcmp(metaToken, "(")) return META_GROUP_START;
    else if (!strcmp(metaToken, ")")) return META_GROUP_END;
    
    else if (!strcmp(metaToken, ">")) return META_WRITE;
    else if (!strcmp(metaToken, "<")) return META_READ;
    else if (!strcmp(metaToken, ">>")) return META_APPEND;
    
    return META_NULL;
}

// 0 - not meta; 1 - is meta; 2 - is double meta
int metaCheck(char ch)
{
    const int metasCount = 7;
    const char metas[] = {'&', '|', ';', '>', '<', '(', ')'};
    const int  doubl[] = { 1,   1,   0,   1,   0,   0,   0 };
    
    int i = 0;
    for (; i < metasCount; i++)
    {
        if (ch == metas[i]) return (doubl[i])? 2 : 1;
    }
    
    return 0;
}

int overflowHandlerP(char ***array, int position, int chunk)
{
    if (!(position % chunk))
    {
        *array = realloc(*array, sizeof(char*) * (position + chunk));
    }
    
    return !(*array);
}

int overflowHandler(char **array, int position, int chunk)
{
    if (!(position % chunk))
    {
        *array = realloc(*array, sizeof(char) * (position + chunk));
    }
    
    return !(*array);
}

void outputTokens(char **tokens, int len)
{
    int token = 0;
    for (; token < len; token++)
    {
        printf("%s\n", tokens[token]);
    }
}

void freeTokens(char ***tokens, int *len)
{
    int token = 0;
    for (; token < *len; token++)
    {
        free((*tokens)[token]);
    }
    *len = 0;
    free(*tokens);
}

int cd(const char *path)
{
    int homeDir = (!path) || (!strcmp(path, "~"));
    int status = (homeDir)? chdir(getenv("HOME")) : chdir(path);
    if (status)
    {
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        return 1;
    }
    
    return 0;
}

int execution(int in, int out, int argc, char **argvSrc)
{
    if (argc == 0)
    {
        shellPerror("Syntax error near unexpected token");
        return 1;
    }
    
    char *argv[argc + 1];
    int i = 0;
    for (; i <= argc; i++)
    {
        argv[i] = argvSrc[i];
    }
    argv[argc] = NULL;
    
    int status;
    pid_t pid;
    
    if ((pid = fork()) < 0)
    {     
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        return 1;
    }
    else if (!pid) // child
    {
        dup2(in, 0);
        dup2(out, 1);
        execvp(argv[0], argv);
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        exit(1);
    }
    else // parent
    {
        while (wait(&status) != pid);
    }
    
    return status;
}

int redirectIO(Meta meta, char *filename, int *in, int *out)
{
    if (determMeta(filename))
    {
        shellPerror("Syntax error near unexpected token");
        return 1;
    }
    
    switch (meta)
    {
        case META_WRITE :
        {
            int fileDescriptor = creat(filename, 0666);
            if (fileDescriptor < 0)
            {
                perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                return 1;
            }
            *out = dup(fileDescriptor);
        }
        break;
        
        case META_READ :
        {
            int fileDescriptor = open(filename, O_RDONLY, 0666);
            if (fileDescriptor < 0)
            {
                perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                return 1;
            }
            *in = dup(fileDescriptor);
        }
        break;
        
        case META_APPEND :
        {
            int fileDescriptor = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0666);
            if (fileDescriptor < 0)
            {
                perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                return 1;
            }
            *out = dup(fileDescriptor);
        }
        break;
        
        default :
        break;
    }
    
    return 0;
}

int runCommand(char **tokens, int in, int out)
{
    if (!(*tokens)) return 0;
    
    char **command = tokens;
    int meta = 0;
    int argc = 0;
    
    int input = dup(in);
    int output = dup(out);
    
    char *token = NULL;
    while ((token = *(tokens++)))
    {
        if ((meta = determMeta(token)) > META_EXPECT_FILE)
        {
            if (redirectIO(meta, *(tokens++), &input, &output)) return 0;
        }
        else
        {
            if (!token || (meta = determMeta(token))) break;
            ++argc;
        }
    }
    
    int status = 0;
    
    if (!strcmp(*command, "cd"))
    {
        if (argc > 2)
        {
            shellPerror("Too many args for 'cd'");
            status = 1;
        }
        else
        {
            status = cd(command[1]);
        }
    }
    else if (!strcmp(*command, "exit"))
    {
        return 1;
    }
    else
    {
        status = execution(input, output, argc, command);
    }
    
    //printf("executed with exit code %d\n", status);
    
    switch (meta)
    {
        case META_EXEC :
        {
            return runCommand(tokens, in, out);
        }
        break;
        
        default : break;
    }
    
    return 0;
}

int parseInput(int fd)
{
    dup2(fd, 0);
    if (!fd && askInput()) return 1;
    
    const int memoryChunk = 20;
    char **tokens = malloc(sizeof(char*) * memoryChunk);
    char *buffer = malloc(sizeof(char) * memoryChunk);
    if (!tokens || !buffer)
    {
        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
        return 1;
    }
    
    int shouldTerminate = 0;
    
    int quotesCount = 0;
    int meta = 0;
    int tokensCount = 0;
    int bufferPos = 0;
    int ch = 0;
    
    
    while((ch = getchar()) != EOF)
    {
        int tokensOK = overflowHandlerP(&tokens, tokensCount + 1, memoryChunk);
        int bufferOK = overflowHandler(&buffer, bufferPos + 1, memoryChunk);
        if (tokensOK || bufferOK)
        {
            perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
            return 1;
        }
        
        int isQuote = (ch == '"');
        quotesCount += isQuote;
        
        if (isQuote)
        {
            continue;
        }
        else if (quotesCount % 2)
        {
            buffer[bufferPos++] = ch;
            if (!fd && (ch == '\n'))
            {
                printf(ANSI_BACKGROUND "|" ANSI_COLOR_RESET " ");
            }
        }
        else
        {
            int charIsMeta = metaCheck(ch);
            int isDoubleMeta = (meta == ch) && (charIsMeta == 2);
            int charIsNewLine = (ch == '\n');
            int charIsSpace = (ch == ' ');
            int stringShoundSplit = charIsNewLine || charIsSpace || charIsMeta;
            
            if (isDoubleMeta)
            {
                char *doubleMeta = malloc(3 * sizeof(char));
                if (!doubleMeta)
                {
                    perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                    return 1;
                }
                doubleMeta[0] = doubleMeta[1] = meta;
                doubleMeta[2] = '\0';
                tokens[tokensCount++] = doubleMeta;
                meta = 0;
            }
            else if (meta)
            {
                char *singleMeta = malloc(2 * sizeof(char));
                if (!singleMeta)
                {
                    perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                    return 1;
                }
                singleMeta[0] = meta;
                singleMeta[1] = '\0';
                tokens[tokensCount++] = singleMeta;
                meta = 0;
            }
            
            if (stringShoundSplit && bufferPos)
            {
                buffer[bufferPos] = '\0';
                tokens[tokensCount++] = buffer;
                buffer = malloc(sizeof(char) * memoryChunk);
                if (!buffer)
                {
                    perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                    return 1;
                }
                bufferPos = 0;
            }
            
            if (charIsNewLine)
            {
                if (tokensCount && metaCheck(tokens[tokensCount - 1][0]))
                {
                    printf(ANSI_BACKGROUND "|" ANSI_COLOR_RESET " ");
                }
                else
                {
                    tokens[tokensCount] = NULL;
                    
                    shouldTerminate = runCommand(tokens, fd, 1);
                    freeTokens(&tokens, &tokensCount);
                    
                    tokens = malloc(sizeof(char*) * memoryChunk);
                    if (!tokens)
                    {
                        perror(ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET);
                        break;
                    }
                    
                    if (shouldTerminate || (!fd && askInput())) break;
                }
            }
            else if (charIsMeta)
            {
                meta = (isDoubleMeta)? 0 : ch;
            }
            else if (!charIsSpace)
            {
                buffer[bufferPos++] = ch;
            }
        }
    }
    
    free(buffer);
    free(tokens);
    
    if (!fd && !shouldTerminate)
    {
        printf("\n");
    }
    else
    {
        close(fd);
    }
    
    return 0;
}

int main(int argc, char **argv)
{
    int fd = 0;
    
    if (argc > 2)
    {
        shellPerror("Invalid number of arguments: default input is being used");
    }
    else if (argc == 2)
    {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0)
        {
            perror(ANSI_ERROR_BG ANSI_BALD "switching to stdio" ANSI_COLOR_RESET);
            fd = 0;
        }
    }
    
    return parseInput(fd);
}
