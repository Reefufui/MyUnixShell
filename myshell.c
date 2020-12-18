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

void shellPerror( char *str )
{
    fprintf( stderr, ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
    fprintf( stderr, ": %s\n", str );
}

char* basename( char *path )
{
    char *s = strrchr( path, '/' );
    return strdup( s + 1 );
}

int askInput()
{
    char cwd[PATH_MAX], usr[HOST_NAME_MAX];
    
    if ( !getcwd( cwd, sizeof( cwd ) ) || getlogin_r( usr, sizeof( usr ) ) )
    {
        perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
        return 1;
    }
    
    char *base = basename( cwd );
    if (  strcmp( cwd, "/" ) ) strcpy( cwd, base );
    if ( !strcmp( cwd, usr ) ) strcpy( cwd, "~"  );
    
    printf( ANSI_BACKGROUND ANSI_BALD );
    printf( "%s %s" ANSI_COLOR_RESET " ", usr, cwd );
    
    free( base );
    return 0;
}

typedef enum {
    META_NULL,
    META_WRITE,
    META_READ,
    META_APPEND,
    META_PIPE,
    META_GROUP_START,
    META_GROUP_END,
    META_PIPELINE,
    META_EXEC,
    META_OR_EXEC,
    META_AND_EXEC,
    META_BACKGROUND,
    
} Meta;

Meta determMeta( char *metaToken )
{
    return ( !metaToken ) ? META_NULL
        : ( !strcmp( metaToken, ";"  ) ) ? META_EXEC
        : ( !strcmp( metaToken, "|"  ) ) ? META_PIPE
        : ( !strcmp( metaToken, "||" ) ) ? META_OR_EXEC
        : ( !strcmp( metaToken, "&&" ) ) ? META_AND_EXEC
        : ( !strcmp( metaToken, "&"  ) ) ? META_BACKGROUND
        : ( !strcmp( metaToken, "("  ) ) ? META_GROUP_START
        : ( !strcmp( metaToken, ")"  ) ) ? META_GROUP_END
        : ( !strcmp( metaToken, ">"  ) ) ? META_WRITE
        : ( !strcmp( metaToken, "<"  ) ) ? META_READ
        : ( !strcmp( metaToken, ">>" ) ) ? META_APPEND
        : META_NULL;
}

int metaCheck( const char ch )
{
    const char metas[] = { '&', '|', ';', '>', '<', '(', ')' };
    const int  doubl[] = {  1,   1,   0,   1,   0,   0 ,  0  };
    
    int i = 7;
    while ( i )
    {
        --i;
        if ( ch == metas[i] ) return ( doubl[i] )? 2 : 1;
    }
    
    return 0;
}

#define OF_HANDLER if ( !( position % chunk ) ) \
   { *array = realloc( *array, sizeof( **array ) * ( position + chunk ) ); } \
   return !( *array );

int overflowHandlerP( char ***array, int position, int chunk ) { OF_HANDLER }
int overflowHandler ( char **array,  int position, int chunk ) { OF_HANDLER }

void outputTokens( char **tokens, int len )
{
    int token = 0;
    for ( ; token < len; token++ )
    {
        printf( "%s\n", tokens[token] );
    }
}

void freeTokens( char ***tokens, int *len )
{
    int token = 0;
    for ( ; token < *len; token++ )
    {
        if ( ( *tokens )[token] ) free( ( *tokens )[token] );
    }
    *len = 0;
    free( *tokens );
}

int cd( const char *path )
{
    int homeDir = ( !path ) || ( !strcmp( path, "~" ) );
    int status = ( homeDir )? chdir( getenv( "HOME" ) ) : chdir( path );
    if ( status )
    {
        perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
        return 1;
    }
    
    return 0;
}

void redirectIO( Meta meta, char *filename )
{
    int fileDescriptor = -1;
    
    if ( meta == META_WRITE )
    {
        fileDescriptor = creat( filename, 0666 );
    }
    else if ( meta == META_APPEND )
    {
        fileDescriptor = open( filename, O_CREAT | O_APPEND | O_WRONLY, 0666 );
    }
    else if ( meta == META_READ )
    {
        fileDescriptor = open( filename, O_RDONLY, 0666 );
    }
    
    if ( fileDescriptor < 0 )
    {
        perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
    }
    else
    {
        dup2( fileDescriptor, ( meta == META_READ ) ? 0 : 1 );
        close( fileDescriptor );
    }
}

typedef struct {
    int isConveyor;
    char **tokens;
    int tokensCount;
    Meta correspSeqMeta;
} Pipeline;

int runSequence( Pipeline *pipeline, int pipelineLength, int *fd );

int runPipeline( Pipeline pipeline, int *fd )
{
    int status = 0;
    pid_t pid = 0;
    
    if ( ( pid = fork() ) < 0 )
    {
        perror( "error" );
    }
    else if ( !pid )
    {
        if ( pipeline.correspSeqMeta != META_BACKGROUND )
        {
            signal(SIGINT, SIG_DFL);
        }
        
        if ( pipeline.isConveyor )
        {
            char *argv[pipeline.tokensCount];
            int token = 0;
            int pipeFD[2] = { -1, -1 };
            
            while ( token < pipeline.tokensCount )
            {
                int argvID = 0;
                int isLastCommand = 0;
                
                for ( ; token < pipeline.tokensCount; token++ )
                {
                    Meta metaToken = determMeta( pipeline.tokens[token] );
                    
                    if ( metaToken )
                    {
                        break;
                    }
                    else
                    {
                        argv[argvID] = pipeline.tokens[token];
                        ++argvID;
                    }
                }
                
                ++token;
                argv[argvID]  = NULL;
                isLastCommand = ( token > pipeline.tokensCount );
                
                pipe( pipeFD );
                
                if ( ( pid = fork() ) < 0 )
                {
                    perror( "error" );
                    _exit( 1 );
                }
                else if ( !pid )
                {
                    if ( !isLastCommand )
                    {
                        dup2( pipeFD[1], fd[1] );
                    }
                    
                    close( pipeFD[0] );
                    close( pipeFD[1] );
                    
                    execvp( argv[0], argv );
                    
                    _exit( 1 );
                }
                
                dup2 ( pipeFD[0], fd[0] );
                close( pipeFD[0]        );
                close( pipeFD[1]        );
            }
            
            int status = 0;
            waitpid( pid, &status, 0 );
            
            _exit( status );
        }
        else
        {
            char *argv[pipeline.tokensCount];
            int token  = 0;
            int argvID = 0;
            
            for ( ; token < pipeline.tokensCount; token++ )
            {
                Meta metaToken = determMeta( pipeline.tokens[token] );
                
                if ( metaToken )
                {
                    redirectIO( metaToken, pipeline.tokens[token + 1] );
                    ++token;
                }
                else
                {
                    argv[argvID] = pipeline.tokens[token];
                    ++argvID;
                }
            }
            
            argv[argvID] = NULL;
            
            execvp( argv[0], argv );
            
            _exit( 1 );
        }
    }
    else if ( pipeline.correspSeqMeta == META_BACKGROUND )
    {
        if ( ( pid = fork() ) < 0 )
        {
            perror( "error" );
        }
        else if ( pid )
        {
            return status;
        }
    }
    
    waitpid( pid, &status, 0 );
    
    if ( pipeline.correspSeqMeta == META_BACKGROUND ) _exit( 0 );
    
    return status;
}

int runSequence( Pipeline *pipelines, int pipelineLength, int *fd )
{
    int pipeID = 0;
    
#if 0
    for ( ; pipeID < pipelineLength; pipeID++ )
    {
        printf( "\n\n$$$\n" );
        printf( "isConveyor\t%d\n", pipelines[pipeID].isConveyor );
        printf( "tokens:\n" );
        outputTokens( pipelines[pipeID].tokens, pipelines[pipeID].tokensCount );
        printf( "tokensCount\t%d\n", pipelines[pipeID].tokensCount );
        printf( "correspSeqMeta\t%d\n", pipelines[pipeID].correspSeqMeta );
    }
#endif
    
    for ( pipeID = 0; pipeID < pipelineLength; pipeID++ )
    {
        int exitCode = runPipeline( pipelines[pipeID], fd );
        if ( pipelines[pipeID].correspSeqMeta != META_BACKGROUND )
        {
            printf( "programm exited with code %d\n", exitCode );
        }
    }
    
    return 0;
}

int allocateTokens( char ***tokens, int memoryChunk )
{
    *tokens = malloc( sizeof( char* ) * memoryChunk );
    if ( !( *tokens ) ) perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
    return !( *tokens );
}

int allocateBuffer( char **buffer, int memoryChunk )
{
    *buffer = malloc( sizeof( char ) * memoryChunk );
    if ( !( *buffer ) ) perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
    return !( *buffer );
}

int proceedTokens( char **tokens, int tokensCount, int *fd )
{
    Pipeline pipelines[tokensCount];
    
    char **pipelineHead = tokens;
    int tokensPos       = 0;
    int pipelinesPos    = 0;
    
    int bracketsOpened  = 0;
    int tokensMet       = 0;
    int pipesMet        = 0;
    
    for ( ; tokensPos < tokensCount; tokensPos++ )
    {
        Meta tokenAsMeta = determMeta( tokens[tokensPos] );
        
        tokenAsMeta = ( tokensPos != tokensCount - 1 ) ? tokenAsMeta
            : ( tokenAsMeta ) ? tokenAsMeta : META_PIPELINE;
        
        tokensMet      += ( tokenAsMeta >  META_PIPELINE    ) ? 0 : 1;
        bracketsOpened += ( tokenAsMeta == META_GROUP_START );
        bracketsOpened -= ( tokenAsMeta == META_GROUP_END   );
        pipesMet       += ( tokenAsMeta == META_PIPE        );
        
        if ( !bracketsOpened )
        {
            if ( tokenAsMeta >= META_PIPELINE )
            {
                pipelines[pipelinesPos].isConveyor     = (pipesMet != 0);
                pipelines[pipelinesPos].tokens         = pipelineHead;
                pipelines[pipelinesPos].tokensCount    = tokensMet;
                pipelines[pipelinesPos].correspSeqMeta = tokenAsMeta;
                
                ++pipelinesPos;
                pipelineHead = &( tokens[tokensPos + 1] );
                
                pipesMet = tokensMet = 0;
            }
        }
    }
    
    return runSequence( pipelines, pipelinesPos, fd );
}

int countTokensInBrackets( char **tokens )
{
    if ( !( *tokens ) ) return -1;
    
    int tokensCount = 0;
    int bracketsToIgnore = 0;
    int tokensPos = 0;
    while ( tokens[tokensPos] )
    {
        bracketsToIgnore += !strcmp( tokens[tokensPos], "(" );
        
        if ( !strcmp( tokens[tokensPos], ")" ) )
        {
            if ( !bracketsToIgnore )
            {
                return tokensCount;
            }
            else
            {
                --bracketsToIgnore;
            }
        }
        
        ++tokensCount;
        ++tokensPos;
    }
    
    return -1;
}

int syntaxCheck( char **tokens, int tokensCount )
{
    int conveyorFlag = 0;
    int redirectionFlag = 0;
    int bracketsFlag = 0;
    
    int tokensPos = 0;
    for ( ; tokensPos < tokensCount; tokensPos++ )
    {
        int isFirst = !tokensPos;
        int isLast = ( tokensPos == tokensCount - 1 );
        
        Meta prevMeta = ( isFirst )? 0 : determMeta( tokens[tokensPos - 1] );
        Meta tokenAsMeta = determMeta( tokens[tokensPos] );
        Meta nextMeta = ( isLast )? 0 : determMeta( tokens[tokensPos + 1] );
        
        switch ( tokenAsMeta )
        {
            case META_READ :
            case META_WRITE :
            case META_APPEND :
            {
                redirectionFlag = 1;
                
                if ( isLast      ||
                    nextMeta     ||
                    conveyorFlag ) return 1;
                break;
            }
            
            case META_PIPE :
            {
                conveyorFlag = 1;
                
                if ( isFirst        ||
                    bracketsFlag    ||
                    prevMeta        ||
                    nextMeta        ||
                    redirectionFlag ) return 1;
                else if ( isLast ) return 2;
                break;
            }
            
            case META_GROUP_START :
            {
                bracketsFlag = 1;
                
                if ( conveyorFlag ) return 1;
                
                ++tokensPos;
                int tokensInBracket = countTokensInBrackets( &( tokens[tokensPos] ) );
                
                if ( tokensInBracket < 0 ) return 2;
                if ( !tokensInBracket ) return 1;
                
                if ( syntaxCheck( &( tokens[tokensPos] ), tokensInBracket ) ) return 1;
                tokensPos += tokensInBracket + 1;
                
                break;
            }
            
            case META_BACKGROUND :
            case META_AND_EXEC :
            case META_OR_EXEC :
            {
                conveyorFlag = bracketsFlag = 0;
                
                if ( isFirst || prevMeta ) return 1;
                int canBeLast = ( tokenAsMeta == META_EXEC );
                canBeLast += ( tokenAsMeta == META_BACKGROUND );
                if ( isLast && !canBeLast ) return 2;
                break;
            }
            
            case META_GROUP_END :
            {
                return 1;
            }
            
            default : continue;
        }
    }
    
    return 0;
}

int myshell( int fd )
{
    dup2( fd, 0 );
    if ( !fd && askInput() ) return 1;
    
    const int memoryChunk = 20;
    char **tokens         = NULL;
    char *buffer          = NULL;
    
    if ( allocateTokens( &tokens, memoryChunk ) ) return 1;
    if ( allocateBuffer( &buffer, memoryChunk ) ) return 1;
    
    int shouldTerminate = 0;
    
    int quotesCount = 0;
    int tokensCount = 0;
    int bufferPos   = 0;
    
    int ch = 0;
    int meta = 0;
    
    while( ( ch = getchar() ) != EOF )
    {
        int tokensOK = overflowHandlerP( &tokens, tokensCount + 1, memoryChunk );
        int bufferOK = overflowHandler( &buffer, bufferPos + 1, memoryChunk );
        if ( tokensOK || bufferOK )
        {
            perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
            return 1;
        }
        
        if ( ch == '"' )
        {
            ++quotesCount;
            continue;
        }
        else if ( quotesCount % 2 )
        {
            buffer[bufferPos++] = ch;
            if ( ch == '\n' )
            {
                if ( !fd ) printf( ANSI_BACKGROUND "|" ANSI_COLOR_RESET " " );
                else return 1;
            }
        }
        else
        {
            int charIsMeta   = metaCheck( ch );
            int isDoubleMeta = ( ch == meta ) && ( charIsMeta == 2 );
            
            if ( isDoubleMeta )
            {
                char *doubleMeta = NULL;
                if ( allocateBuffer( &doubleMeta, 3 ) ) return 1;
                doubleMeta[0] = doubleMeta[1] = meta;
                meta = doubleMeta[2] = '\0';
                tokens[tokensCount++] = doubleMeta;
            }
            else if ( meta )
            {
                char *singleMeta = NULL;
                if ( allocateBuffer( &singleMeta, 2 ) ) return 1;
                singleMeta[0] = meta;
                meta = singleMeta[1] = '\0';
                tokens[tokensCount++] = singleMeta;
            }
            
            int charIsNewLine     = ( ch == '\n' );
            int charIsSpace       = ( ch == ' '  );
            int stringShoundSplit = charIsNewLine || charIsSpace || charIsMeta;
            
            if ( stringShoundSplit && bufferPos )
            {
                bufferPos = buffer[bufferPos] = '\0';
                tokens[tokensCount++] = buffer;
                if ( allocateBuffer( &buffer, memoryChunk ) ) return 1;
            }
            
            if ( charIsNewLine )
            {
                tokens[tokensCount] = NULL;
                int syntaxStatus = syntaxCheck( tokens, tokensCount );
                
                if ( ( syntaxStatus == 1 ) || ( syntaxStatus && fd ) )
                {
                    shellPerror( "Invalid syntax" );
                    freeTokens( &tokens, &tokensCount );
                    if ( allocateTokens( &tokens, memoryChunk ) ) return 1;
                    if ( !fd && askInput() ) break;
                }
                else if ( syntaxStatus == 2 )
                {
                    printf( ANSI_BACKGROUND "|" ANSI_COLOR_RESET " " );
                }
                else
                {
                    pid_t pid = -1;
                    if ( tokens[0] && !strcmp( tokens[0], "cd" ) )
                    {
                        if ( tokens[1] && tokens[2] )
                        {
                            shellPerror( "cd : too many arguments" );
                        }
                        else
                        {
                            cd( tokens[1] );
                        }
                    }
                    else if ( tokens[0] && !strcmp( tokens[0], "exit" ) )
                    {
                        shouldTerminate = 1;
                    }
                    else if ( ( pid = fork() ) < 0 )
                    {     
                        perror( ANSI_ERROR_BG ANSI_BALD "error" ANSI_COLOR_RESET );
                        return 1;
                    }
                    else if ( !pid )
                    {
                        int initFD[2] = { 0, 1 };
                        exit( proceedTokens( tokens, tokensCount, initFD ) );
                    }
                    else waitpid( pid, &shouldTerminate, 0 );
                    
                    //outputTokens( tokens, tokensCount );
                    
                    freeTokens( &tokens, &tokensCount );
                    if ( allocateTokens( &tokens, memoryChunk ) ) return 1;
                    if ( shouldTerminate || ( !fd && askInput() ) ) break;
                }
            }
            else if ( charIsMeta )
            {
                meta = ( isDoubleMeta )? 0 : ch;
            }
            else if ( !charIsSpace )
            {
                buffer[bufferPos++] = ch;
            }
        }
    }
    
    free( buffer );
    free( tokens );
    
    if ( !fd && !shouldTerminate ) printf( "\n" );
    else close( fd );
    
    return 0;
}

int main( int argc, char **argv )
{
    int fd = 0;
    
    if ( argc > 2 )
    {
        shellPerror( "Invalid number of arguments: default input is being used" );
    }
    else if ( argc == 2 )
    {
        fd = open( argv[1], O_RDONLY );
        if ( fd < 0 )
        {
            perror( ANSI_ERROR_BG ANSI_BALD "switching to stdio" ANSI_COLOR_RESET );
            fd = 0;
        }
    }
    
    signal(SIGINT, SIG_IGN);
    return myshell( fd );
}
