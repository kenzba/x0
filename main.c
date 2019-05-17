// Simple X-0 game
// Malyarov Evgenii

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

// Row and column count in X-0 field
#define COUNT 3

// ----------------------------------------

char** newCells()
{
	char **cells = malloc(COUNT * sizeof(char*));
	for (int i = 0; i < COUNT; ++i)
	{
		cells[i] = malloc(COUNT * sizeof(char));
		for (int j = 0; j < COUNT; ++j)
			cells[i][j] = '.';
	}
	return cells;
}

int freeCells(char **cells)
{
	for (int i = 0; i < COUNT; ++i)
		free(cells[i]);
	free(cells);
	return 0;
}

// ----------------------------------------

int recvCoord(FILE *f, int *row, int *col)
{
	fpurge(f);
	int n = fscanf(f, "%d %d", row, col);
	return (n == 2) ? (0) : (-1);
}

int sendHelp(FILE *f)
{
	fprintf(f, "fire: ROW COL\n");
	fflush(f);
	return 0;
}

// Send X-0 field
int sendCells(FILE *f, char *cells[])
{
	fprintf(f, "  ");
	for (int i = 0; i < COUNT; ++i)
		fprintf(f, "%d", i);
	fprintf(f, "\n");

	fprintf(f, " +");
	for (int i = 0; i < COUNT; ++i)
		fprintf(f, "-");
	fprintf(f, "\n");

	for (int i = 0; i < COUNT; ++i)
	{
		fprintf(f, "%d|", i);
		for (int j = 0; j < COUNT; ++j)
			fprintf(f, "%c", cells[i][j]);
		fprintf(f, "\n");
	}

	fflush(f);
	return 0;
}

int checkCoord(int row, int col, char **cells)
{
	if (row < 0 || row >= COUNT || col < 0 || col >= COUNT)
		return -2;
	else if (cells[row][col] == 'X' || cells[row][col] == '0')
		return -1;
	else
		return 0;
}

int findSequence(char **cells, char ch)
{
	int cntH, cntV, cntDD, cntDI;
	cntDD = cntDI = 0;
	for (int i = 0; i < COUNT; ++i)
	{
		cntH = cntV = 0;
		for (int j = 0; j < COUNT; ++j)
		{
			cntH += (cells[i][j] == ch) ? 1 : 0; // horizontal
			cntV += (cells[j][i] == ch) ? 1 : 0; // vertical
		}
		cntDD += (cells[i][i] == ch) ? 1 : 0; // diagonal direct
		cntDI += (cells[i][COUNT - 1 - i] == ch) ? 1 : 0; // diagonal inverse

		if (cntH == COUNT || cntV == COUNT)
			return 0; // winner detected
	}
	if (cntDD == COUNT || cntDI == COUNT)
		return 0; // winner detected

	return -1; // there is no winner
}

int calcWinner(char **cells)
{
	int err;

	// Is X-player winner?
	err = findSequence(cells, 'X');
	if (!err)
		return 0; // winner is first player (index 0)

	// Is 0-player winner?
	err = findSequence(cells, '0');
	if (!err)
		return 1; // winner is second player (index 1)

	// Search for empty cells
	for (int i = 0; i < COUNT; ++i)
		for (int j = 0; j < COUNT; ++j)
			if (cells[i][j] != 'X' && cells[i][j] != '0')
				return -1; // continue game

	// No empty cells! This is dead heat!
	return 2;
}

// ----------------------------------------

int processGame(FILE *playerX, FILE *player0)
{
	char **cells = newCells();
	int row, col;
	int indx;
	FILE *players[] = {playerX, player0};

	// Help
	sendHelp(players[0]);
	sendHelp(players[1]);

	// Game cycle
	for (int step = 0; 1; ++step)
	{
		// Check EOF
		if (feof(players[0]) || feof(players[1]))
		{
			perror("Break connection\n");
			goto GAME_OVER;
		}

		// Send current situation
		sendCells(players[0], cells);
		sendCells(players[1], cells);

		// Is game over?
		int win = calcWinner(cells);
		switch (win)
		{
			case 0:
			case 1:
				indx = win;
				fprintf(players[indx], "You are winner!\n");
				fflush(players[indx]);
				indx = (indx + 1) % 2;
				fprintf(players[indx], "You are loser!\n");
				fflush(players[indx]);
				goto GAME_OVER;
			case 2:
				for (indx = 0; indx < 2; ++indx)
				{
					fprintf(players[indx], "Dead heat!\n");
					fflush(players[indx]);
				}
				goto GAME_OVER;
		}

		// Send message to player for waiting
		indx = (step + 1) % 2;
		fprintf(players[indx], "Please, wait. Your opponent is thinking ...\n");
		fflush(players[indx]);

		// Player step
		indx = step % 2;
		while (1)
		{
			// Prompt string
			char fire = (indx == 0) ? 'X' : '0';
			fprintf(players[indx], "%c-fire coordinate> ", fire);
			fflush(players[indx]);

			// Get coordinate
			int err = recvCoord(players[indx], &row, &col);
			if (err < 0)
			{
				fprintf(players[indx], "Bad coordinate\n");
				fflush(players[indx]);
				continue;
			}

			// Validate coordinate
			err = checkCoord(row, col, cells);
			if (err < 0)
			{
				fprintf(players[indx],
					(err == -2)
					? "Coordinate out of bound!\n"
					: "Coordinate already in use!\n"
				);
				fflush(players[indx]);
				continue;
			}

			// Make step
			cells[row][col] = fire;

			break;
		}
	}

GAME_OVER:
	freeCells(cells);
	return 0;
}

// ----------------------------------------

int main(int argc , char *argv[])
{
	int err; 

	// Create socket
	int srvsock = socket(PF_INET, SOCK_STREAM, 0);
	if (srvsock < 0)
	{
		perror("Could not create socket");
		return EXIT_FAILURE;
	}

	struct sockaddr_in srvaddr;
	memset(&srvaddr, 0, sizeof(srvaddr));
	srvaddr.sin_family      = AF_INET;
	srvaddr.sin_port        = htons(8888);
	srvaddr.sin_addr.s_addr = INADDR_ANY;

	err = bind(srvsock, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
	if (err < 0)
	{
		perror("bind error");
		close(srvsock);
		return EXIT_FAILURE;
	}

	err = listen(srvsock, 10);
	if (err < 0)
	{
		perror("listen error");
		close(srvsock);
		return EXIT_FAILURE;
	}

	// Accept connection cicle
	FILE *playerX;
	FILE *player0;
	for (int playerID = 0; 1; ++playerID)
	{
		printf("Waiting player ...\n");

		// Accept player connection
		struct sockaddr_in clnaddr;
		socklen_t clnlen;
		int consock = accept(srvsock, (struct sockaddr *)&clnaddr, &clnlen);
		if (consock < 0)
		{
			perror("accept error");
			--playerID;
			continue;
		}

		char fire = ((playerID % 2) == 0) ? 'X' : '0';
		printf("Connected %c-player (ID=%d)\n", fire, playerID);

		// Associate a stream with the existing file descriptor
		FILE *client = fdopen(consock, "w+");
		if (client == NULL)
		{
			perror("fdopen error");
			close(consock);
			--playerID;
			continue;
		}

		// Greeting the player
		fprintf(client, "You are %c-player (ID=%d)\n", fire, playerID);
		if (fire != '0')
			fprintf(client, "Please, wait your opponent ...\n");
		fflush(client);

		if (fire == '0')
			player0 = client;
		else
			playerX = client;

		// Check for 2 players
		if (fire == '0')
		{
			// We have 2 palayers! Try start the game...
			int pid = fork();
			if (pid == -1)
			{
				perror("fork error");
				fclose(playerX);
				fclose(player0);
			}
			else if (pid == 0)
			{
				// Child process
				close(srvsock);
				processGame(playerX, player0); // game cycle

				// Game over
				printf("Game over for palayers %d and %d\n",
					playerID - 1, playerID);
				fclose(playerX);
				fclose(player0);
				return EXIT_SUCCESS;
			}
			else
			{
				// Parent process
				printf("Start X-0 game for palayers %d and %d. Child PID: %d\n",
					playerID - 1, playerID, pid);
				fclose(playerX);
				fclose(player0);
			}
		}

		// Continue accept new connections
		// ...
	}

	// Waiting child processes
	while (waitpid(-1, NULL, 0) > 0)
		;

	close(srvsock);
	return EXIT_SUCCESS;
}
