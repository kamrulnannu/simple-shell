#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <glob.h>

static int executeCMD(char *args[], bool);
static void myWrite(int fd, const char* msg);
static void trim(char* buff);
static void redirectToStdIn(const char *fileName);
static void redirectToStdOut(const char *fileName);
static int BatchMode(char*filename);
static bool isBuiltin(const char*cmd, int* cmdIndex);
static int execBuiltIn(int i, char** args);
static int executeCD(char ** cmd, int count);
static int executeWHICH(char** args, int count);
static int executePWD(char** cmd, int count);
static bool CreateAndCheckCmdAbsPath(char*AbsPath, char* cmd);
static bool GetAbsCmdPath(char* path, char* cmd);
static void dealloc(char**, int count);
static char** expandWildcards(char* token, int*count);
static void BuiltInCmdRedir(char* outputfile, char** args, int CMDidx);
static void freePathList();

#define CMD_BUFF 2048 // The maximum length command
static int processCMD(char*cmd);
char * PrepareBuffForTokenize(char *input);
void createPipe(char *args[]);

static int forever_run = 1;  // flag to determine when to exit program
static char **PathList = NULL;
static int PathCount = 0;
static char* builtin [3];
static int numBuiltinCMD = 3;
static char MyProgName[20];
static int g_status= 0;
static const int SUCCESS =0;
static const int FAIL =1;
static char g_EchoPath[PATH_MAX+1];

static void dealloc(char** args, int count)
{
   for(int i=0; i<count; i++)
   {
       if(args[i])
       {
        free(args[i]);
        args[i]=NULL;
       }
   }
}

static void freePathList()
{
    //myWrite(STDOUT_FILENO, "INFO: freeing memory...\n");
    dealloc(PathList, PathCount);
    free(PathList);
    PathList = NULL;
    PathCount = 0;
}

static char** expandWildcards(char* token, int*count)
{
    char**filelist = NULL;
    *count =0;
    glob_t glob_result;
    if (glob(token, GLOB_NOCHECK, NULL, &glob_result) == 0) {
        // Replace the original token with expanded paths
        filelist = (char**)calloc(glob_result.gl_pathc, sizeof(char*));
        *count = glob_result.gl_pathc;
        // Append any additional matches
        for (int j = 0; j < glob_result.gl_pathc; ++j) {
            filelist[j]=strdup(glob_result.gl_pathv[j]);
        }
        // Free resources used by glob
        globfree(&glob_result);
    }
    else
    {
       char**filelist = (char**)calloc(1, sizeof(char*));
       filelist[0]=strdup(token);
       *count = 1;
    }
    return filelist;
}
static bool CreateAndCheckCmdAbsPath(char*AbsPath, char* cmd)
{
    if(cmd[0]=='/')
    {
         strcpy(AbsPath, cmd);
        if(access(AbsPath, F_OK | R_OK | X_OK)==0)
        {
            return true;
        }
    }
    else
    {
        for(int i =0; i<PathCount; i++)
        {
            strcpy(AbsPath, PathList[i]);
            strcat(AbsPath, "/");
            strcat(AbsPath, cmd);
            if(access(AbsPath, F_OK | R_OK | X_OK)==0)
            {
                return true;
            }
        }
    }
    return false;
}
static bool GetAbsCmdPath(char* path, char* cmd)
{
    if(!CreateAndCheckCmdAbsPath(path, cmd))
    {
        myWrite(STDERR_FILENO, cmd);
        myWrite(STDERR_FILENO, ": Not Found\n");
        return false;
    }
    return true;
}

static bool isBuiltin(const char*cmd, int* cmdIndex)
{
  for(int i =0; i<numBuiltinCMD; i++)
  {
     if (strcmp(cmd, builtin[i])==0)
     {
        *cmdIndex = i;
        return true;
     }
  }
  return false;
}
static int CountCmdArgs(char**args)
{
  int count =0;
  while(args[count])
  {
    count++;
  }
  return count;
}
static int execBuiltIn(int i, char** args)
{
   int count = CountCmdArgs(args);
   if(i==0)
   {
     return executeCD(args, count);
   }
   if(i == 1)
   {
    return executePWD(args,count);
   }
   if (i == 2)
   {
    return executeWHICH(args, count);
   }
   return FAIL;
}
static int executeCD(char ** cmd, int count)
{
   if(count!=2)
   {
     myWrite(STDERR_FILENO, "Invalid Argument for cd\n");
     return FAIL;
   }
   else
   {
     if(chdir(cmd[1])!=0)
     {
        myWrite(STDERR_FILENO, "chdir() failed\n");
        return FAIL;
     }
    return SUCCESS;
   }
}
static int executeWHICH(char** args, int count)
{
    int CMDidx;
    char AbsPath[PATH_MAX+1];
    if (isBuiltin(args[1], &CMDidx) || count!=2)
    {
        return FAIL;
    }
    if(CreateAndCheckCmdAbsPath(AbsPath, args[1]))
    {
      myWrite(STDOUT_FILENO, AbsPath);
      myWrite(STDOUT_FILENO, "\n");
      return SUCCESS;
    }

  return FAIL;
}
static int executePWD(char** cmd, int count)
{
    char temp [PATH_MAX+1];
    if(getcwd(temp, sizeof(temp))!=NULL)
    {
        myWrite(STDOUT_FILENO, temp);
        myWrite(STDOUT_FILENO, "\n");
        return SUCCESS;
    }
    else
    {
        myWrite(STDERR_FILENO, "get cwd() failed\n");
        return FAIL;
    }
}

static int BatchMode(char*filename)
{
    char buff[CMD_BUFF];
    memset(buff, 0, sizeof(buff));
    char workbuff[2048];
    memset(workbuff, 0, sizeof(workbuff));
    int FD = open(filename, O_RDONLY);
    if(FD == -1)
    {
      myWrite(STDERR_FILENO, "BatchMode: open failed\n");
      return 1;
    }
    ssize_t readcount;
    char* beginLine = workbuff;
    while((readcount = read(FD, buff, sizeof(buff)-1))>0)
    {
      int currlen = strlen(workbuff);
      snprintf(workbuff + currlen, sizeof(workbuff)-currlen, "%s", buff);
      memset(buff, 0, sizeof(buff));
      char*endLine = strchr(workbuff, '\n');
      while(endLine)
      {
         // found new line
         const int linelen = endLine - beginLine;
         char aLine[linelen+1];
         memcpy(aLine, workbuff, linelen);
         aLine[linelen] = '\0';
         //fprintf(stdout, "%s\n", aLine);
         g_status=processCMD(aLine);
         const int remainLen = strlen(workbuff)-linelen -1;
         char rembuff [remainLen+1];
         snprintf(rembuff, sizeof(rembuff), "%s", endLine+1);
         snprintf(workbuff, sizeof(workbuff), "%s", rembuff);
         endLine = strchr(workbuff, '\n');
      }
    }
     if(strlen(workbuff)> 0)
       {
          g_status=processCMD(workbuff);
         //fprintf(stdout, "%s\n", workbuff);
       }
       close(FD);
       return 0;
}
static int processCMD(char*buffCMD)
{
    char *args[CMD_BUFF];
    int status =0;
    char *tokens;
    int CMDidx;
    tokens = PrepareBuffForTokenize(buffCMD);
    bool isPipe = false;
    char *tok = strtok(tokens, " ");
    int i = 0;
    bool isBuiltInRedir = false;
    bool isOutRedir = false;
    bool isThen = false;
    bool isElse = false;
    while (tok) {
        if (*tok == '<') {
            char* inputfile = strtok(NULL, " ");
            redirectToStdIn(inputfile);
        } else if (*tok == '>') {
             isOutRedir = true;
            char*outputfile= strtok(NULL, " ");
            if(isBuiltin(args[0], &CMDidx))
            {
                //printf("==============%s\n", args[0]);
               BuiltInCmdRedir(outputfile, args, CMDidx);
               isBuiltInRedir =true;
            }
            else
            {
                redirectToStdOut(outputfile);
                isBuiltInRedir=false;
            }
        } else if (*tok == '|') {
             if(i==0)
             {
                myWrite(STDERR_FILENO, "Syntax Error: No command Before Pipe\n");
                free(tokens);
                return 1;
             }
             if(strcmp("cd", args[0])==0)
             {
                myWrite(STDERR_FILENO, "Syntax Err : Pipe is Not Allowed with cd Command\n");
                free(tokens);
                dealloc(args, i);
                return 1;
             }
            isPipe = true;
            args[i] = NULL;
            if(isOutRedir)
            {
                isOutRedir = false;
                break;
            }
            //createPipe(args);
            i = 0;
        }
        else if(strcmp(tok, "then")==0 && (i == 0))
        {
            isThen = true;
            if(isElse == true)
            {
         //       myWrite(STDERR_FILENO, "then and else cannot be in the same command line\n");
                 free(tokens);
                return g_status;
            }
            if(isPipe == true)
            {
              myWrite(STDERR_FILENO, "then token is not allowed after pipe\n");
                return g_status;
            }
            if(g_status!=0)
            {
                char msg[1024];
                snprintf(msg, sizeof(msg), "then: prev command failed with status:%d\n", g_status);
           //     myWrite(STDERR_FILENO, msg);
                 free(tokens);
                return g_status;
            }
        }
        else if(strcmp(tok, "else")==0 && (i == 0))
        {
            isElse =true;
            if(isThen == true)
            {
             //   myWrite(STDERR_FILENO, "else and then cannot be in the same command line\n");
                 free(tokens);
                return g_status;
            }
            if(isPipe == true)
            {
               myWrite(STDERR_FILENO, "else token is not allowed after pipe\n");
                return g_status;
            }
            if(g_status==0)
            {
          //       myWrite(STDERR_FILENO, "else: prev command executed sucessfully with status 0\n");
                  free(tokens);
                 return g_status;
            }
        }
        else
        {
            if(isPipe)
            {
              createPipe(args);
              isPipe = false;
            }
            if(i>0 && strchr(tok, '*'))
            {
                int count=0;
              char ** filelist = expandWildcards(tok, &count);
              //printf("expanded: [%s]\n", expanded);
              for(int k =0; k<count; k++)
              {
                args[i]= filelist[k];
                i++;
              }

              free(filelist);
            }
            else
            {
                args[i] = strdup(tok);
                i++;
            }
        }
        tok = strtok(NULL, " ");
    }
    if(isPipe && i==0)
    {
        myWrite(STDERR_FILENO, "Syntax Error: No Command After Pipe\n");
        free(tokens);
        return 1;
    }
    args[i] = NULL;

    if(args[0] != NULL && !isBuiltInRedir)
    {
        status = executeCMD(args, false);
    }
    dealloc(args, i);
   free(tokens);
   return status;

}
static void InitShell()
{
    const char *path = "PATH";
    char * path_list = getenv(path);
    if (!path_list)
    {
        myWrite(STDERR_FILENO, "Err: getenv failed!!!");
        exit(1);
    }
    int count = 0;
    char *tmp = strtok(path_list, ":");
    while (tmp != NULL)
    {
        short bFound = 0;
        //printf("%s\n", tmp);
        for (int i = 0; i < PathCount; ++i)
        {
            // Check for duplicate path
            if (strcmp(PathList[i], tmp) == 0)
            {
                bFound = 1;
                break;
            }
        }
        if (!bFound)
        {
            if (count == PathCount)
            {
                count += 5;
                PathList = (char **)realloc(PathList, count * sizeof(char *));
            }
            if (PathList)
            {
                char *p  = strdup(tmp);
                if (!p)
                {
                    myWrite(STDERR_FILENO, "Err: strdup failed!!");
                    exit(1);
                }
                PathList[PathCount++] = p;
            }
            else
            {
                myWrite(STDERR_FILENO, "Err: realloc failed!!");
                exit(1);
            }
        }
        tmp = strtok(NULL, ":");
    }

    builtin[0]="cd";
    builtin[1]="pwd";
    builtin[2]="which";

    bool st = GetAbsCmdPath(g_EchoPath, "echo");
    if (st == false)
    {
        // Error is already logged
        strcpy(g_EchoPath, "/bin/echo");
    }
}

static void trim(char* buff)
{
    const int len = strlen(buff);
    char temp[len+1];
    memset(temp, 0, sizeof(temp));
    int Bindex = 0;
    int Eindex = len-1;
    while (buff[Bindex] == ' ' || buff[Bindex]=='\t')
    {
        Bindex++;
    }
    if(len == Bindex)
    {
      memset(buff, 0, len);
      return;
    }
    while(buff[Eindex]==' '|| buff[Eindex]== '\t' || buff[Eindex]=='\n')
    {
        Eindex--;
    }
    strncpy(temp, &buff[Bindex], Eindex - Bindex +1);
    strcpy(buff, temp);
}
static void redirectToStdIn(const char *fileName)
{
    if(fileName == NULL)
    {
        myWrite(STDERR_FILENO, " RedirectToStdIn: NULL File Name\n");
        exit(1);
    }
    int fd = open(fileName, O_RDONLY);
    if (fd==-1)
    {
        myWrite(STDERR_FILENO, "RedirectToIn: open failed\n");
        exit(1);
    }

    dup2(fd, STDIN_FILENO);
    close(fd);
}
static void BuiltInCmdRedir(char* outputfile, char** args, int CMDidx)
{
    if(outputfile == NULL)
    {
        myWrite(STDERR_FILENO, "BuiltInCmdRedir: NULL File Name\n");
        exit(1);
    }
    int fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP );
    if(fd == -1)
    {
        myWrite(STDERR_FILENO, "BuiltInCmdRedir: open failed:");
        myWrite(STDERR_FILENO, outputfile);
        exit(1);
    }
    int count = CountCmdArgs(args);
    int so_fd = dup(STDOUT_FILENO);
    dup2(fd, STDOUT_FILENO);
    close(fd);
    if(CMDidx==1) //pwd
    {
       char cwd[1024];
      myWrite(STDOUT_FILENO, getcwd(cwd, sizeof(cwd)));
      myWrite(STDOUT_FILENO, "\n");
    }
    else if(CMDidx==2)
    {
        int idx;
        char AbsPath[PATH_MAX+1];
        if (isBuiltin(args[1], &idx) || count!=2)
        {
            dup2(so_fd, STDOUT_FILENO);
            return;
        }
        if(CreateAndCheckCmdAbsPath(AbsPath, args[1]))
        {
            myWrite(STDOUT_FILENO, AbsPath);
            myWrite(STDOUT_FILENO, "\n");
        }

    }
    dup2(so_fd, STDOUT_FILENO);
}
static void redirectToStdOut(const char *fileName)
{
    if(fileName == NULL)
    {
        myWrite(STDERR_FILENO, " RedirectToStdOut: NULL File Name\n");
        exit(1);
    }
    int fd = open(fileName, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP );
    if(fd == -1)
    {
        myWrite(STDERR_FILENO, "RedirectToOut: open failed:");
        myWrite(STDERR_FILENO, fileName);
        exit(1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
}
static void myWrite(int fd, const char* msg)
{
  write(fd, msg, strlen(msg));
}
/**
 * Runs a command.
 *
 * @param *args[] the args to run
 */
static int executeCMD(char *args[], bool isPipe)
{

     if(args[0] == NULL)
     {
        myWrite(STDERR_FILENO, "NULL CMD RECIEVED\n");
        return 1;
     }
    int CmdIndex;
    char AbsPath[PATH_MAX+1];
    if (strcmp(args[0], "exit") == 0)
    {
        myWrite(STDOUT_FILENO, MyProgName);
        myWrite(STDOUT_FILENO, ": Bye Bye\n");
        //Builtin
         forever_run = 0;
         return 0;
    }
    bool isBuiltIn = false;
    if(isBuiltin(args[0],&CmdIndex))
    {
       isBuiltIn = true;
      if(!isPipe)
      {
         return execBuiltIn(CmdIndex, args);
      }
    }
    if(!isBuiltIn)
    {
        bool st = GetAbsCmdPath(AbsPath, args[0]);
        if(st == false)
            return 1;
    }
    //printf("===============%s\n", AbsPath);
    int status =0;
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        myWrite(STDERR_FILENO,"Fork Failed\n");
        return 1;
    }
    else if ( pid == 0) { /* child process */
        if(!isBuiltIn)
        {
            if (execv(AbsPath, args)<0)
            {
            myWrite(STDERR_FILENO, "executeCMD : execv failed");
            return 1;
            }
        }
      else
      {
         char * BiArgs[3];
         BiArgs[0]= g_EchoPath;
         BiArgs[1]=NULL;
         BiArgs[2]=NULL;
        if(CmdIndex==1) // pwd
        {
            char cwd[1024];
            getcwd(cwd, sizeof(cwd));
            BiArgs[1]= cwd;
          //  execv(echoPath, BiArgs);
        }
        else if(CmdIndex==2) // which
        {
            int count = CountCmdArgs(args);
            int idx;
            char AbsPath[PATH_MAX+1];
             if (isBuiltin(args[1], &idx) || count!=2)
             {
                BiArgs[1]=NULL;
          //      execv(echoPath, BiArgs);
             }
            else if(CreateAndCheckCmdAbsPath(AbsPath, args[1]))
            {
                BiArgs[1]=AbsPath;
        //        execv(echoPath, BiArgs);
            }
        }
        execv(BiArgs[0], BiArgs);
      }
    }
    else { /* parent process */
    //printf("p1\n");
            waitpid(pid, &status, 0);
           // printf("p2\n");
    }
    redirectToStdIn("/dev/tty");
    redirectToStdOut("/dev/tty");
    return status;

}


void createPipe(char *args[])
{
    int fd[2];
    if(pipe(fd)<0){
        myWrite(STDERR_FILENO, "CreatePipe: Pipe Failed\n");
        exit(1);
    }

    dup2(fd[1], STDOUT_FILENO);
    close(fd[1]);

    executeCMD(args, true);

    dup2(fd[0], STDIN_FILENO);
    close(fd[0]);
}


char * PrepareBuffForTokenize(char *input)
{
    int i;
    int j = 0;
    char *tokenized = (char *)calloc((CMD_BUFF * 2) , sizeof(char));
    if (!tokenized)
    {
        myWrite(STDERR_FILENO, "Err: calloc failed!!!\n");
        exit(1);
    }

    /* Add space around every token which doesn't have space already
    so that we can tokenize using strtok
    Example: Input= cat x>abc, Output=cat x > abc
    */
    for (i = 0; i < strlen(input); i++) {
        if (input[i] != '>' && input[i] != '<' && input[i] != '|') {
            tokenized[j++] = input[i];
        } else {
            tokenized[j++] = ' ';
            tokenized[j++] = input[i];
            tokenized[j++] = ' ';
        }
    }

    return tokenized;
}

/**
 * Runs a basic shell.
 *
 * @return 0 upon completion
 */
int main(int argc, char**argv)
{
    atexit(freePathList);
     // command line arguments
    if(strncmp(argv[0], "./", 2 )==0)
    {
        strcpy(MyProgName, &argv[0][2]);
    }
    else
    {
        strcpy(MyProgName, argv[0]);
    }
    if(argc > 2)
    {
        myWrite(STDERR_FILENO, MyProgName);
        myWrite(STDERR_FILENO, ": Invalid Arguments\n");
        exit(1);
    }
    InitShell();
    if(argc==2)
    {
        g_status = BatchMode(argv[1]);
        return g_status;
    }
    myWrite(STDOUT_FILENO, "Welcome To My Simple Shell!!!\n");
    while (forever_run) {
        myWrite(STDOUT_FILENO,"shell> ");

        char buff[CMD_BUFF];
        memset(buff, 0, sizeof(buff));
        read(STDIN_FILENO, buff, sizeof(buff)-1);
        trim(buff);
        if(strlen(buff)==0)
        {
            continue;
        }
        g_status= processCMD(buff);
    }
    return g_status;
}
