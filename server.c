#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define NUM_WORDS 480
#define PORT 8080
#define MAX_GUESSES 8
#define NUM_ROUNDS 5

// Function prototypes
int setupServer(int playercount);
void acceptNewConnection(int server_fd, int client_sockets[], int playercount, struct sockaddr_in* address, int addrlen);
void retrieveClientNickname(int playercount, int client_sockets[], fd_set* readfds, char* player_names[]);
char* getPlayerName(int sd, char** player_name);
char* randomWord();
void playRound(int playercount, int client_sockets[], char* player_names[], int player_scores[]);
void sendLeaderboard(int playercount, int client_sockets[], char* player_names[], int player_scores[]);

int main(void) {
    int playercount;
    printf("Insert player count: ");
    scanf("%d", &playercount);

    int client_sockets[playercount];
    char* player_names[playercount];
    int player_scores[playercount];  
    memset(player_scores, 0, sizeof(player_scores));  
    memset(client_sockets, 0, sizeof(client_sockets));  
    memset(player_names, 0, sizeof(player_names)); 

    int server_fd = setupServer(playercount);

    struct sockaddr_in address;
    int addrlen = sizeof(address);

    printf("Hangman Lobby Open. Waiting for Players to Join...\n");
    int connected_players = 0;
    fd_set readfds;
    
    while (connected_players < playercount) {
        FD_ZERO(&readfds);  // Clear the set
        FD_SET(server_fd, &readfds);  // Add the server socket to the set

        int max_sd = server_fd;

        // Add client sockets to the set and determine max socket descriptor
        for (int i = 0; i < playercount; i++) {
            int sd = client_sockets[i];
            if (sd > 0) FD_SET(sd, &readfds);
            if (sd > max_sd) max_sd = sd;
        }

        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Select failed");
            exit(EXIT_FAILURE);
        }

        // Handle new connections
        if (FD_ISSET(server_fd, &readfds)) {
            acceptNewConnection(server_fd, client_sockets, playercount, &address, addrlen);
        }

        // Retrieve nicknames for clients who haven't provided it yet
        retrieveClientNickname(playercount, client_sockets, &readfds, player_names);
        
        // Count how many players have connected and provided their nicknames
        connected_players = 0;
        for (int i = 0; i < playercount; i++) {
            if (player_names[i] != NULL) {
                connected_players++;
            }
        }
    }

    printf("All players have joined. Starting game...\n");
    playRound(playercount, client_sockets, player_names, player_scores);

    return 0;
}

// Setup server socket,
int setupServer(int playercount) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Allow reuse of the same address
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, playercount) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

// Accept a new connection
void acceptNewConnection(int server_fd, int client_sockets[], int playercount, struct sockaddr_in* address, int addrlen) {
    int new_socket;
    if ((new_socket = accept(server_fd, (struct sockaddr*)address, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < playercount; i++) {
        if (client_sockets[i] == 0) {
            client_sockets[i] = new_socket;
            printf("New connection: Client %d connected\n", i + 1);
            break;
        }
    }
}

// Retrieve client nickname (only once per client)
void retrieveClientNickname(int playercount, int client_sockets[], fd_set* readfds, char* player_names[]) {
    for (int i = 0; i < playercount; i++) {
        int sd = client_sockets[i];
        if (FD_ISSET(sd, readfds)) {
            if (player_names[i] == NULL) {
                // First message from client is the player's name
                if (getPlayerName(sd, &player_names[i]) == NULL) {
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    printf("Player %d has joined with nickname: %s\n", i + 1, player_names[i]);
                }
            }
        }
    }
}

// Receive and store player's nickname
char* getPlayerName(int sd, char** player_name) {
    uint8_t nickname_length;

    // Receive nickname length
    if (recv(sd, &nickname_length, sizeof(nickname_length), 0) <= 0) {
        return NULL;
    }

    // Allocate memory for nickname
    *player_name = (char*)malloc(nickname_length + 1); // +1 for null terminator

    // Receive the nickname
    if (recv(sd, *player_name, nickname_length, 0) <= 0) {
        free(*player_name);
        *player_name = NULL;
        return NULL;
    }

    // Null-terminate the string
    (*player_name)[nickname_length] = '\0';

    return *player_name;
}

// Function to play a round of the game
void playRound(int playercount, int client_sockets[], char* player_names[], int player_scores[]) {
    char* word = randomWord();  // Generate the word for the round
    int word_len = strlen(word);
    
    // Array to track progress for each client
    int guesses_left[playercount];
    int* player_progress[playercount]; // Each client will have their own boolean array to track progress

    for (int i = 0; i < playercount; i++) {
        player_progress[i] = (int*)calloc(word_len, sizeof(int));  // Initialize all progress arrays to 0s (not guessed)
    }

    for (int i = 0; i < playercount; i++) {
        for(int j= 0 ; j < word_len ; j++)
        {
        player_progress[i][j] = 0; // Initialize all progress arrays to 0s (not guessed)
        }
    
    }

    // Step 2: Send word length to each client
    for (int i = 0; i < playercount; i++) {
        if (client_sockets[i] != 0) {
            send(client_sockets[i], &word_len, sizeof(word_len), 0);
            guesses_left[i] = MAX_GUESSES;  // Initialize guesses for each player
        }
    }
    
    int game_active = 1;

    while (game_active) {
        game_active = 0;  // Assume no active players, will update in the loop

        // Step 3: Process guesses from each client
        for (int i = 0; i < playercount; i++) {
            if (client_sockets[i] != 0 && guesses_left[i] > 0) {
                game_active = 1;  // At least one player is still guessing

                char guess;
                char *boolean_array = malloc(word_len * sizeof(char)); // Boolean array for client i
                for(int i=0; i<=word_len ; i++)
                {
                    boolean_array[i] = '0';
                }
                boolean_array[word_len] = '\0';
               
                if (recv(client_sockets[i], &guess, sizeof(guess), 0) > 0) {
                   
                    // Step 4: Check if guessed char is in the word
                    int updated = 0;  // Tracks whether the word has been updated with new found letters

                    for (int j = 0; j < word_len; j++) {
                        if (word[j] == guess && player_progress[i][j] == 0) {
                            player_progress[i][j] = 1;  // Mark the letter as found
                            updated = 1;
                        }
                        int progress_value = player_progress[i][j];  // Get the int value from player_progress
                        // Convert the int (0 or 1) to the corresponding char ('0' or '1')
                        char tt = progress_value ? '1' : '0'; 
                        boolean_array[j] =  tt; // Copy current progress to the boolean array
                    }
                      ssize_t ss= send(client_sockets[i], boolean_array, strlen(boolean_array), 0);
                    // Step 5: Send boolean array to client
                     if(ss <=0)
                     {
                        perror("Send error: ");

                     }
                     else
                     {
                        printf("bytes send: %zd" , ss);
                     }

                    // Step 6: Check if the client has won
                    if (!updated) {
                        guesses_left[i]--;  // Deduct a guess for a wrong letter
                    }

                    // check guess
                    int solved = 1;
                    for (int j = 0; j < word_len; j++) {
                        if (player_progress[i][j] == 0) {
                            solved = 0;
                            break;
                        }
                    }

                    if (solved) {
                        int score = word_len * 10;  // Example scoring system
                        player_scores[i] += score;
                    }

                    if (guesses_left[i] <= 0) {
                        printf("Player %d has used up all their guesses!\n", i + 1);
                    }
                }
                else
                {
                    perror("Failed: ");
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    // Step 7: Send the leaderboard
    sendLeaderboard(playercount, client_sockets, player_names, player_scores);

    // Free allocated memory for player progress arrays
    for (int i = 0; i < playercount; i++) {
        free(player_progress[i]);
    }
}

// Send the leaderboard to all players
void sendLeaderboard(int playercount, int client_sockets[], char* player_names[], int player_scores[]) {
    char leaderboard[1024] = "";  // Large buffer to hold leaderboard data

    // Construct the leaderboard string
    strcat(leaderboard, "Leaderboard:\n");
    for (int i = 0; i < playercount; i++) {
        char entry[128];
        sprintf(entry, "Player %d (%s): %d points\n", i + 1, player_names[i], player_scores[i]);
        strcat(leaderboard, entry);
    }

    // Send the leaderboard to each client
    for (int i = 0; i < playercount; i++) {
        if (client_sockets[i] != 0) {
            send(client_sockets[i], leaderboard, strlen(leaderboard), 0);
        }
    }
}

//generate random word from list
char * randomWord(){
    
    char* selectedWord;
    char *words[] = {  
    "mountain", "river", "ocean", "forest", "desert", "valley", "prairie", "jungle", "tundra", "volcano",
    "island", "beach", "harbor", "canyon", "plateau", "summit", "glacier", "cliff", "waterfall", "horizon",
    "sunrise", "sunset", "thunder", "lightning", "rainbow", "whirlpool", "sandstorm", "tornado", "avalanche", "earthquake",
    "meadow", "garden", "orchard", "vineyard", "pasture", "farmland", "wilderness", "grove", "swamp", "marsh",
    "school", "college", "library", "museum", "gallery", "stadium", "theater", "hospital", "station", "university",
    "airplane", "helicopter", "submarine", "scooter", "bicycle", "motorcycle", "bus", "tram", "subway", "train",
    "police", "fireman", "doctor", "nurse", "teacher", "lawyer", "judge", "pilot", "engineer", "scientist",
    "artist", "musician", "painter", "sculptor", "writer", "author", "director", "actor", "dancer", "singer",
    "student", "professor", "librarian", "manager", "worker", "clerk", "cashier", "waiter", "barista", "chef",
    "computer", "keyboard", "monitor", "printer", "scanner", "router", "modem", "speaker", "tablet", "camera",
    "mountain", "desert", "island", "valley", "forest", "river", "prairie", "ocean", "tundra", "waterfall",
    "software", "hardware", "network", "database", "browser", "program", "system", "server", "backup", "virtual",
    "python", "java", "csharp", "golang", "kotlin", "swift", "binary", "array", "vector", "pointer",
    "fiction", "novel", "poetry", "drama", "comedy", "tragedy", "biography", "mystery", "fantasy", "romance",
    "justice", "freedom", "honesty", "integrity", "loyalty", "compassion", "patience", "courage", "respect", "wisdom",
    "biology", "chemistry", "physics", "geology", "astronomy", "botany", "zoology", "ecology", "genetics", "microbes",
    "algorithm", "equation", "formula", "theorem", "calculus", "geometry", "algebra", "statistics", "integral", "matrix",
    "robotics", "cybernetics", "nanotech", "quantum", "gravity", "relativity", "telescope", "microscope", "satellite", "probe",
    "horizon", "galaxy", "planet", "comet", "asteroid", "meteor", "nebula", "quasar", "pulsar", "blackhole",
    "stadium", "ballpark", "court", "arena", "gym", "track", "field", "rink", "pool", "raceway",
    "soccer", "basketball", "baseball", "football", "hockey", "volleyball", "tennis", "cricket", "rugby", "golf",
    "ballet", "opera", "concert", "festival", "parade", "exhibit", "circus", "performance", "competition", "audition",
    "guitar", "piano", "violin", "drums", "trumpet", "saxophone", "flute", "cello", "trombone", "clarinet",
    "imagine", "create", "invent", "design", "solve", "analyze", "explore", "discover", "develop", "build",
    "dialogue", "character", "setting", "theme", "plot", "conflict", "climax", "resolution", "narrative", "scene",
    "problem", "solution", "method", "process", "hypothesis", "experiment", "result", "conclusion", "evidence", "data",
    "dinosaur", "mammal", "reptile", "insect", "amphibian", "species", "organism", "ecosystem", "habitat", "predator",
    "economy", "market", "currency", "finance", "investment", "trade", "industry", "business", "capital", "taxes",
    "republic", "monarchy", "democracy", "dictator", "senator", "president", "governor", "mayor", "minister", "judge",
    "culture", "society", "community", "tradition", "ritual", "custom", "language", "religion", "belief", "values",
    "history", "timeline", "dynasty", "empire", "kingdom", "revolution", "warfare", "battle", "treaty", "independence",
    "program", "project", "assignment", "task", "deadline", "goal", "strategy", "meeting", "discussion", "plan",
    "robot", "drone", "machine", "automation", "sensor", "microchip", "circuit", "gadget", "interface", "controller",
    "company", "startup", "corporation", "agency", "bureau", "office", "branch", "firm", "subsidiary", "enterprise",
    "resource", "supply", "distribution", "demand", "management", "inventory", "production", "operation", "maintenance", "logistics",
    "leader", "team", "group", "collaborate", "negotiate", "coordinate", "support", "assist", "consult", "evaluate",
    "website", "blog", "forum", "social", "platform", "media", "application", "content", "service", "support",
    "earth", "planet", "mars", "venus", "jupiter", "saturn", "mercury", "uranus", "neptune", "pluto",
    "apple", "banana", "grapes", "orange", "melon", "mango", "peach", "cherry", "pear", "plum",
    "dream", "imagine", "create", "wonder", "discover", "explore", "build", "invent", "learn", "grow",
    "smile", "laugh", "cry", "sigh", "yawn", "shout", "whisper", "scream", "talk", "sing",
    "run", "jump", "walk", "dance", "swim", "climb", "crawl", "slide", "stretch", "spin",
    "shirt", "pants", "jacket", "scarf", "gloves", "hat", "shoes", "socks", "belt", "boots",
    "phone", "tablet", "laptop", "camera", "remote", "speaker", "headphones", "battery", "charger", "monitor",
    "pencil", "eraser", "marker", "notebook", "ruler", "scissors", "glue", "tape", "paper", "folder",
    "river", "lake", "stream", "pond", "waterfall", "spring", "bay", "ocean", "sea", "fjord",
    "cloud", "rain", "snow", "hail", "wind", "fog", "storm", "lightning", "thunder", "breeze",
    "sun", "moon", "stars", "planet", "comet", "meteor", "asteroid", "galaxy", "universe", "space"
};
    
    srand(time(NULL));//time seed
    int drawn_index = rand() % NUM_WORDS;
    return words[drawn_index];
}