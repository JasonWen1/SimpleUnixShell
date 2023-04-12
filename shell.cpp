#include<bits/stdc++.h>
#include<unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
using namespace std;

vector<char*> history_node;
char* begin_path = "/bin";



typedef struct PathNode {
	char *path;
	struct PathNode* next;
}PathNode;


const char* error_message = "An error has occurred\n";


int getInputs(char* line, char *a[128]) {
	char *delim = "\n \t\r\f\v";
	int i = 0;
	char *input ;
	input = strtok(line, delim);
	while(input != NULL) {
		a[i] = input;
		i = i + 1;
		input = strtok(NULL, delim);
	}
	return i;
}

void outputHistory(vector<char*> history, int n) {
	int len = min(n, (int)history.size());

	for(int i = 0; i < len; i++) {
		cout << history[i] << endl;
	}
}

inline void outputError() {
	write(STDERR_FILENO, error_message, strlen(error_message));
}

int main(int argc, char *argv[]) {
	if(argc > 2) {
		outputError();
	}
	else {
		FILE* f = NULL;

		//determine batch mode or interactive mode
		if(argc == 2) {
			//batch mode
			//open the batch file
			f = fopen(argv[1], "r");
			if(f == NULL) {
				outputError();
				exit(1);
			}

		}
		else {
			//interactive mode
			f = stdin;
		}
		PathNode* pn = (PathNode *)malloc(sizeof(PathNode));
		pn->path = strdup(begin_path);
		pn->next = NULL;


		while(1) {

			if(argc == 1) {
				cout << "shell> ";
				fflush(stdout);
			}
		
			//readlines
			char* line = NULL;
			size_t buffer = 0;
			if(getline(&line, &buffer, f) == EOF) {
				exit(0);
			}
			//ignore empty line
			if (strcmp(line, "\n") == 0) {
				continue;
			}
			line = strtok(line, "\n");
			history_node.push_back(strdup(line));
			
			//consider the command with | and >
			char* line1 = (char *) malloc ((strlen(line) + 50) * sizeof(char));
			int cur = 0;
	
			for(int i = 0; i < strlen(line); i++) {
				if(line[i] == '>' || line[i] == '|') {
					line1[cur++] = ' ';
					line1[cur++] = line[i];
					line1[cur++] = ' ';
				}
				else {
					line1[cur++] = line[i];
				}
			}
			line1[cur++] = '\0';
			free(line);

			char* a[128];
			char* holder[128];
			//process command
			int arg_num = getInputs(line1, a);
			//if arg number equals to 0, we need move to next line
			if(!arg_num) continue;

			int pipe_num = 0;
			int redirect_num = 0;
			int pipe_pos = -1;
			int redirect_pos = -1;
			int pipefd[2];
			//count the number and the position of pipe and redirection
			for(int i = 0; i < arg_num; i++) {
				if(strcmp(a[i], ">") == 0) {
					redirect_num++;
					redirect_pos = i;
				}
				else if(strcmp(a[i], "|") == 0) {
					pipe_num++;
					pipe_pos = i;
				}
			}
			//check valid
			if(redirect_num + pipe_num > 1) {
				outputError();
				free(line1);
				continue;
			}
			//handle command with redirect
			if(redirect_num) {
				//check valid
				int back_num = arg_num - 1 - redirect_pos;
				if(back_num != 1 || redirect_pos == 0) {
					outputError();
					free(line1);
					continue;
				}
				a[redirect_pos] = NULL;
			}
			//handle command with pipe
			if(pipe_num) {
				//check valid
				int back_num = arg_num - 1 - pipe_pos;
				if(back_num == 0 ||  pipe_pos == 0) {
					outputError();
					free(line1);
					continue;
				}
				//core function
				pipe(pipefd);
				int cnt = 0;
				for(int i = pipe_pos + 1; i < arg_num; i++) {
					holder[cnt++] = a[i];
				}
				a[pipe_pos] = NULL;
				holder[cnt] = NULL;
			}
			//check each command
			if(strcmp(a[0], "exit") == 0) {
				if(arg_num > 1) {
					outputError();
					free(line1);
					continue;
				}
				exit(0);
			}
			else if(strcmp(a[0], "cd") == 0) {
				if(arg_num != 2) {
					outputError();
					free(line1);
					continue;
				}
				int res = chdir(a[1]);
				if(res) {
					outputError();
					free(line1);
					continue;
				}
			} 
			else if(strcmp(a[0], "history") == 0) {
				if(arg_num > 2) {
					outputError();
					free(line1);
					continue;
				}
				else if(arg_num == 1) {
					outputHistory(history_node, INT_MAX);
				}
				else {
					int nums = ceil(atof(a[1]));
					outputHistory(history_node, nums);
				}
			}
			
			else if(strcmp(a[0], "path") == 0) {
				//we have created pathnode outside the loop
				//free the path first
				while(pn != NULL) {
					PathNode* cur = pn;
					pn = pn->next;
					free(cur);
				}

				//new path
				for(int i = 1; i < arg_num; i++) {
					PathNode* cur = (PathNode *)malloc(sizeof(PathNode));
					cur->path = strdup(a[i]);
					cur->next = pn;
					pn = cur;
				}
			}

			//the most difficult part is path and pipe related
			else {
				PathNode* temp = pn;
				PathNode* temp1 = pn;//temp1 for fork return value is not 0
				bool perm = false;
				bool perm1 = false;
				int execv_flag = 0;
				bool fork_stat = true;
				while(temp != NULL) {
					char* path1 = strdup(temp->path);
					strcat(path1, "/");
					strcat(path1, a[0]);
					a[arg_num] = NULL;
					/*
					The access() system call checks the accessibility of the file named by the
     				path argument for the access permissions indicated by the mode argument.
     				The value of mode is either the bitwise-inclusive OR of the access
     				permissions to be checked (R_OK for read permission, W_OK for write
     				permission, and X_OK for execute/search permission), or the existence test
     				(F_OK).
					*/
					if(access(path1, X_OK) == 0) {
						perm = true;
						int rc = fork();
						if(rc == 0) { //child
						/*
						In dup2(), the value of the new descriptor fildes2 is specified.  If fildes
     					and fildes2 are equal, then dup2() just returns fildes2; no other changes
     					are made to the existing descriptor.  Otherwise, if descriptor fildes2 is
     					already in use, it is first deallocated as if a close(2) call had been done
     					first.
						*/
							if(redirect_num == 1) {
								int fd = open(a[arg_num-1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
								dup2(fd, 1);
								dup2(fd, 2);
								close(fd);
							}

							if(pipe_num == 1) {
								close(pipefd[0]);
								// make stdout go to file
								dup2(pipefd[1], 1);
								dup2(pipefd[1], 2);
								close(pipefd[1]);
							}

							execv_flag = execv(path1, a);
						}
						else if(rc == -1) {
							fork_stat = false;
						}
						else {
							rc = (int) wait(NULL);

							if(pipe_num == 1) {
								while(temp1 != NULL) {
									char* path2 = strdup(temp1->path);
									strcat(path2, "/");
									strcat(path2, holder[0]);
									if(access(path2, X_OK) == 0) {
										perm1 = true;
										int pipe_rc = fork();

										if(pipe_rc == 0) {
											close(pipefd[1]);    // close writing end in the child
											dup2(pipefd[0], 0);  // send stdin to the pipe
											close(pipefd[0]);
											execv_flag = execv(path2, holder);
										}
										else if(pipe_rc == -1) {
											fork_stat = false;
										}
										else {
											close(pipefd[1]);
											close(pipefd[0]);
											pipe_rc = (int) wait(NULL);
										}
									}
									free(path2);
									temp1 = temp1->next;
								}
							}
						}
					}
					free(path1);
					temp = temp->next;
				}
				//error handle
				if(execv_flag == -1 || fork_stat == false || perm == false || (pipe_num == 1 && perm1 == false)) {
					//to file
					if(redirect_num == 1) {
						int fd = open(a[arg_num - 1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
						outputError();
						close(fd);
					}
					//to default
					else {
						outputError();
					}
				}
			}
			for(int i = 0; i < arg_num; i++) {
				a[i] = NULL;
				holder[i] = NULL;
			}
			free(line1);
		}
	}
	return 0;
}