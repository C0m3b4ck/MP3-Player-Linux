#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

enum class Language { EN, PL };

Language language = Language::EN;

void exit_program(int code = 0) {
    std::cout << (language == Language::PL ? "Zamykanie programu.\n" : "Exiting program.\n");
    std::exit(code);
}

void enable_raw_mode(termios& orig_termios) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;   // Wait for at least 1 char (blocking read)
    raw.c_cc[VTIME] = 0;  // No timeout
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void disable_raw_mode(const termios& orig_termios) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

char getch_blocking() {
    char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) return c;
    return 0;
}

int read_int_validated(int min_val, int max_val, const std::string& prompt) {
    int val;
    while (true) {
        std::cout << prompt;
        if (std::cin >> val) {
            if (val >= min_val && val <= max_val) {
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                return val;
            } else {
                std::cout << (language == Language::PL ?
                    "Wartość poza zakresem (" + std::to_string(min_val) + "-" + std::to_string(max_val) + "). Spróbuj ponownie.\n" :
                    "Input out of range (" + std::to_string(min_val) + "-" + std::to_string(max_val) + "). Try again.\n");
            }
        } else {
            std::cout << (language == Language::PL ? "Niepoprawne dane. Wprowadź liczbę.\n" : "Invalid input. Please enter a number.\n");
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }
}

bool run_amixer(const std::string& cmd) {
    int ret = std::system(cmd.c_str());
    return (ret == 0);
}

void volume_up() {
    if (run_amixer("amixer set Master 5%+ > /dev/null 2>&1") ||
        run_amixer("amixer set PCM 5%+ > /dev/null 2>&1")) {
        std::cout << (language == Language::PL ? "Głośność zwiększona" : "[Volume increased]") << std::endl;
    } else {
        std::cout << (language == Language::PL ? "Nie udało się zwiększyć głośności" : "[Failed to increase volume]") << std::endl;
    }
}

void volume_down() {
    if (run_amixer("amixer set Master 5%- > /dev/null 2>&1") ||
        run_amixer("amixer set PCM 5%- > /dev/null 2>&1")) {
        std::cout << (language == Language::PL ? "Głośność zmniejszona" : "[Volume decreased]") << std::endl;
    } else {
        std::cout << (language == Language::PL ? "Nie udało się zmniejszyć głośności" : "[Failed to decrease volume]") << std::endl;
    }
}

void print_playback_help() {
    if (language == Language::PL) {
        std::cout << R"(
Sterowanie podczas odtwarzania:
  s lub spacja   : Pauza/Wznowienie odtwarzania
  B             : Głośniej
  -             : Ciszej
  q             : Zakończ odtwarzanie
  h             : Pokaż tę pomoc
Uwaga: Zaawansowane klawisze (f,d,b,o,p itd.) wymagają implementacji.
)" << std::endl;
    } else {
        std::cout << R"(
Playback Controls:
  s or space    : Pause/Resume playback
  B             : Volume up
  -             : Volume down
  q             : Quit current playback
  h             : Show this help
Note: Advanced keys (f,d,b,o,p etc.) require further implementation.
)" << std::endl;
    }
}

void play_file_interactive(const fs::path& file_path) {
    std::cout << (language == Language::PL ? "Odtwarzanie: " : "Playing: ") << file_path.filename() << "\n";
    print_playback_help();

    pid_t pid = fork();
    if (pid == 0) {
        execlp("mpg123", "mpg123", "-q", file_path.c_str(), nullptr);
        _exit(127);
    } else if (pid > 0) {
        termios orig_termios;
        enable_raw_mode(orig_termios);

        bool paused = false;
        bool quit = false;

        while (!quit) {
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) break;

            char c = getch_blocking();
            if (c) {
                switch (c) {
                    case 's':
                    case ' ':
                        paused = !paused;
                        if (paused) {
                            if (std::system("pkill -STOP mpg123") == 0)
                                std::cout << (language == Language::PL ? "Pauza\n" : "[Paused]\n");
                            else
                                std::cout << (language == Language::PL ? "Nie udało się wstrzymać\n" : "[Pause failed]\n");
                        } else {
                            if (std::system("pkill -CONT mpg123") == 0)
                                std::cout << (language == Language::PL ? "Wznowiono\n" : "[Resumed]\n");
                            else
                                std::cout << (language == Language::PL ? "Nie udało się wznowić\n" : "[Resume failed]\n");
                        }
                        break;
                    case 'B':
                    case 'b':
                        volume_up();
                        break;
                    case '-':
                        volume_down();
                        break;
                    case 'q':
                        std::system("pkill mpg123");
                        quit = true;
                        break;
                    case 'h':
                        print_playback_help();
                        break;
                }
            }
        }

        disable_raw_mode(orig_termios);
    } else {
        std::cerr << (language == Language::PL ? "Błąd: nie udało się wykonać fork.\n" : "Error: failed to fork playback process.\n");
    }
}

void print_main_menu() {
    if (language == Language::PL) {
        std::cout << R"(

Menu główne:
  1 : Odtwórz pojedynczy plik
  2 : Odtwórz sekwencyjnie od początku
  3 : Odtwórz sekwencyjnie od końca
  4 : Wyjdź
)" << std::endl;
    } else {
        std::cout << R"(
Main menu options:
  1 : Play single file
  2 : Play sequentially front-to-back
  3 : Play sequentially back-to-front
  4 : Exit
)" << std::endl;
    }
}

void print_playback_controls_header() {
    if (language == Language::PL) {
        std::cout << R"(
Sterowanie podczas odtwarzania:

)";
    } else {
        std::cout << R"(During playback controls:

)";
    }
}

int main() {
    // Ask user for language on startup
    std::cout << "Select language / Wybierz Język / (EN/pl): ";
    std::string lang_input;
    std::getline(std::cin, lang_input);
    if (!lang_input.empty() && (lang_input == "pl" || lang_input == "PL")) {
        language = Language::PL;
    } else {
        language = Language::EN;
    }

    std::string mp3_dir;
    std::string language_setting;

    // Read mp.conf ignoring previous language in favor of user choice above — but still get mp3_dir from file
    std::ifstream conf("mp.conf");
    if (conf.is_open()) {
        std::string line;
        while (std::getline(conf, line)) {
            if (line.rfind("language=", 0) == 0) {
                // skip to honor runtime selection instead
                continue;
            } else if (mp3_dir.empty()) {
                mp3_dir = line;
            }
        }
        conf.close();
    }

    std::cout << R"(
    ___  _________ _____  ______ _       _____   _____________
    |  \/  || ___ \____ | | ___ \ |     / _ \ \ / /  ___| ___ \
    | .  . || |_/ /   / / | |_/ / |    / /_\ \ V /| |__ | |_/ /
    | |\/| ||  __/    \ \ |  __/| |    |  _  |\ / |  __||    /
    | |  | || |   .___/ / | |   | |____| | | || | | |___| |\ \
    \_|  |_/\_|   \____/  \_|   \_____/\_| |_/\_/ \____/\_| \_|
)" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << R"(
    _     _____ _   _ _   ___   __
   | |   |_   _| \ | | | | \ \ / /
   | |     | | |  \| | | | |\ V /
   | |     | | | . ` | | | |/   \
   | |_____| |_| |\  | |_| / /^\ \
   \_____/\___/\_| \_/\___/\/   \/
)" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << R"(
    ______ _____ _____ ___
    | ___ \  ___|_   _/ _ \
    | |_/ / |__   | |/ /_\ \
    | ___ \  __|  | ||  _  |
    | |_/ / |___  | || | | |
    \____/\____/  \_/\_| |_/
)" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << (language == Language::PL ? "Odtwarzacz MP3, wersja Beta 1.0.0 dla Linuksa\n\n" : "MP3 Player, version Beta 1.0.0 for Linux\n\n");

    if (mp3_dir.empty()) {
        std::cout << (language == Language::PL ? "Podaj ścieżkę katalogu z plikami .mp3: " : "Enter directory path containing .mp3 files: ");
        std::getline(std::cin, mp3_dir);
        std::cout << (language == Language::PL ? "Zapisz katalog do mp.conf? (t/n): " : "Save directory to mp.conf? (y/n): ");
        char save_choice;
        std::cin >> save_choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (save_choice == 'y' || save_choice == 'Y' || save_choice == 't' || save_choice == 'T') {
            std::ofstream conf_out("mp.conf");
            if (conf_out) {
                if (!mp3_dir.empty())
                    conf_out << mp3_dir << "\n";
                if (language == Language::PL)
                    conf_out << "language=pl\n";
                else
                    conf_out << "language=en\n";
                conf_out.close();
                std::cout << (language == Language::PL ? "Katalog zapisany.\n\n" : "Directory saved.\n\n");
            } else {
                std::cerr << (language == Language::PL ? "Nie udało się zapisać konfiguracji.\n\n" : "Failed to save config.\n\n");
            }
        }
    }

    if (!fs::is_directory(mp3_dir)) {
        std::cerr << (language == Language::PL ? "Katalog nie istnieje. Kończenie programu.\n" : "Directory does not exist. Exiting.\n");
        exit_program(1);
    }

    std::vector<fs::path> mp3_files;
    for (auto& e : fs::directory_iterator(mp3_dir))
        if (e.path().extension() == ".mp3")
            mp3_files.push_back(e.path());

    if (mp3_files.empty()) {
        std::cerr << (language == Language::PL ? "Nie znaleziono plików .mp3.\n" : "No .mp3 files found.\n");
        exit_program(1);
    }

    bool exit_app = false;
    while (!exit_app) {
        if (language == Language::PL) {
            std::cout << R"(

Menu główne:
  1 : Odtwórz pojedynczy plik
  2 : Odtwórz sekwencyjnie od początku
  3 : Odtwórz sekwencyjnie od końca
  4 : Wyjdź
)";
        } else {
            std::cout << R"(
Main menu options:
  1 : Play single file
  2 : Play sequentially front-to-back
  3 : Play sequentially back-to-front
  4 : Exit
)";
        }

        std::cout << (language == Language::PL ? "Wybierz opcję odtwarzania (1-4): " : "Select playback option (1-4): ");
        int choice = read_int_validated(1, 4, "");

        if (choice == 4) {
            exit_app = true;
            break;
        }

        if (language == Language::PL) {
            std::cout << R"(
Sterowanie podczas odtwarzania:

)";
            print_playback_help();
        } else {
            std::cout << R"(During playback controls:

)";
            print_playback_help();
        }

        if (choice == 1) {
            std::cout << (language == Language::PL ? "Dostępne pliki:\n" : "Available files:\n");
            for (size_t i = 0; i < mp3_files.size(); ++i)
                std::cout << i + 1 << ") " << mp3_files[i].filename() << "\n";

            int file_choice = read_int_validated(1, (int)mp3_files.size(),
                language == Language::PL ? "Wprowadź numer pliku do odtworzenia: " : "Enter file number to play: ");
            play_file_interactive(mp3_files[file_choice - 1]);
        } else if (choice == 2) {
            for (auto& file : mp3_files) {
                play_file_interactive(file);
            }
        } else if (choice == 3) {
            for (auto it = mp3_files.rbegin(); it != mp3_files.rend(); ++it) {
                play_file_interactive(*it);
            }
        }
        std::cout << std::endl;  // Separate next menu prompt
    }

    std::cout << (language == Language::PL ? "Do widzenia!\n" : "Goodbye!\n");
    return 0;
}
