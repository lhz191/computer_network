#include <iostream>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <mutex>
#include <vector>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <memory>
#include <map>
#include <windows.h> // For Windows console color
#include <chrono>
#pragma comment(lib, "ws2_32.lib")
using namespace std;

const int MAX_CLIENTS = 100;
const int BUFFER_SIZE = 1024;
const string EXIT_COMMAND = "q";
const string HELP_COMMAND1 = "/help";
const string UPLOAD_COMMAND = "/upload";
const string PRIVATE_MESSAGE_COMMAND = "/pm";
const string KICK_COMMAND = "/kick";
const string BAN_COMMAND = "/ban";
const string CLEAR_COMMAND = "/clear";
const string SAVE_COMMAND = "/save";
const string LOAD_COMMAND = "/load";
const string STATUS_COMMAND = "/status";
const string ADDFRDCOMMAND = "/addfrd";
const string ADDSBCOMMAND = "/addsb";
map<string, bool> userStatusMap;
mutex clientsMutex;

void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void printWelcomeScreen() {
    clearScreen();
    cout << "************************************************\n";
    cout << "**********   欢迎进入多人聊天系统   ************\n";
    cout << "************************************************\n\n";
    cout << "请输入消息或命令 (如: /upload 发送文件, 'q' 退出，/help获取功能索引):\n";
}

struct ClientInfo {
    SOCKET sockfd;
    string username;
    string ipAddress;
    atomic<bool> isOnline;
    vector<string> friendsList; // 好友列表
    vector<string> sbList; // 黑名单
    ClientInfo(SOCKET s, const string& u, const string& ip, bool o)
        : sockfd(s), username(u), ipAddress(ip), isOnline(o) {}

    ClientInfo(const ClientInfo&) = delete;
    ClientInfo& operator=(const ClientInfo&) = delete;
};

// 打印在线用户状态
void printUserStatus() {
    lock_guard<mutex> lock(clientsMutex);
    cout << "\n======= 在线用户列表 =======\n";

    // 传统方式遍历 userStatusMap
    for (map<string, bool>::iterator it = userStatusMap.begin(); it != userStatusMap.end(); ++it) {
        cout << it->first << (it->second ? " (在线)" : " (离线)") << endl;
    }

    cout << "============================\n";
}

void setConsoleColor(int color) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
#endif
}

void logMessage(const string& message, int color = 7) {
    ofstream logFile("chat_log.txt", ios::app);
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm timeInfo;
    localtime_s(&timeInfo, &now);
    setConsoleColor(color);
    cout << put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << " - " << message << endl;
    setConsoleColor(7);
    logFile << put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << " - " << message << endl;
}

vector<shared_ptr<ClientInfo>> clients;

void broadcastMessage(const string& message, int color = 7) {
    lock_guard<mutex> lock(clientsMutex);
    for (const auto& client : clients) {
        if (client->isOnline) {
            send(client->sockfd, message.c_str(), message.length(), 0);
        }
    }
    logMessage("Broadcast: " + message, color);
}

bool uploadFile(SOCKET clientSocket) {
    char filename[BUFFER_SIZE];
    int bytesReceived = recv(clientSocket, filename, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        cerr << "Failed to receive file name." << endl;
        return false;
    }
    filename[bytesReceived] = '\0';
    //cout << 11111111111 << endl;
    char fileSizeBuffer[BUFFER_SIZE];
    bytesReceived = recv(clientSocket, fileSizeBuffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        cerr << "Failed to receive file size." << endl;
        return false;
    }
    int fileSize = stoi(fileSizeBuffer);

    ofstream outFile(filename, ios::binary);
    if (!outFile.is_open()) {
        cerr << "Failed to create file: " << filename << endl;
        return false;
    }

    // 将日志信息写入日志文件
    ofstream logFile("upload_log.txt", ios::app);
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm timeInfo;
    localtime_s(&timeInfo, &now);
    logFile << put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << " - Receiving file: " << filename << " (" << fileSize << " bytes)" << endl;

    char buffer[BUFFER_SIZE];
    int totalBytesReceived = 0;
    while (totalBytesReceived < fileSize && (bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        outFile.write(buffer, bytesReceived);
        totalBytesReceived += bytesReceived;
    }

    outFile.close();
    logFile << put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << " - File received: " << filename << " (" << totalBytesReceived << " bytes)" << endl;
    logFile.close();

    return totalBytesReceived == fileSize;
}
string sanitizeMessage(const string& input) {
    if (input.length() > BUFFER_SIZE - 1) {
        return input.substr(0, BUFFER_SIZE - 1);
    }
    return input;
}
// 获取与指定套接字关联的用户名
string getUsernameFromSocket(SOCKET sockfd) {
    for (auto& client : clients) {
        if (client->sockfd ==sockfd) {
            return client->username;
        }
    }
    return "";
}
// 处理添加好友请求
void handleAddFriendRequest(const string& requesterUsername, const string& friendUsername) {
    std::shared_ptr<ClientInfo> requester = nullptr;
    std::shared_ptr<ClientInfo> friendUser = nullptr;
    // 使用 for 循环查找请求者
    for (auto& client : clients) {
        if (client->username == requesterUsername) {
            requester = client;
        }
        if (client->username == friendUsername) {
            friendUser = client;
        }
        // 一旦找到两者，退出循环
        if (requester && friendUser) break;
    }

    if (!requester) {
        // 请求者不存在
        return;
    }
    if (!friendUser) {
        // 好友不存在
        return;
    }
    // 将好友添加到请求者的好友列表
    requester->friendsList.push_back(friendUsername);
}
std::string printblackList(const string& username) {
    // 查找用户
    std::shared_ptr<ClientInfo> user = nullptr;

    // 使用 for 循环查找用户
    for (auto& client : clients) {
        if (client->username == username) {
            user = client;
            break;
        }
    }

    // 检查用户是否存在
    if (!user) {
        return "用户 '" + username + "' 不存在。\n";  // 返回用户不存在的消息
    }

    // 创建字符串流以存储黑名单信息
    std::ostringstream oss;
    oss << "\n======= " << username << " 的黑名单 =======\n";
    if (user->sbList.empty()) {
        oss << "当前没有黑名单。\n";
    }
    else {
        for (const auto& SBName : user->sbList) {
            oss << "- " << SBName << "\n";
        }
    }
    oss << "============================\n";

    // 返回生成的字符串
    return oss.str();
}
std::string printFriendsList(const string& username) {
    // 查找用户
    std::shared_ptr<ClientInfo> user = nullptr;

    // 使用 for 循环查找用户
    for (auto& client : clients) {
        if (client->username == username) {
            user = client;
            break;
        }
    }

    // 检查用户是否存在
    if (!user) {
        return "用户 '" + username + "' 不存在。\n";  // 返回用户不存在的消息
    }

    // 创建字符串流以存储好友列表信息
    std::ostringstream oss;
    oss << "\n======= " << username << " 的好友列表 =======\n";
    if (user->friendsList.empty()) {
        oss << "当前没有好友。\n";
    }
    else {
        for (const auto& friendName : user->friendsList) {
            oss << "- " << friendName << "\n";
        }
    }
    oss << "============================\n";

    // 返回生成的字符串
    return oss.str();
}


// 处理添加黑名单请求
void handleAddSbRequest(const string& requesterUsername, const string& sbUsername) {
    std::shared_ptr<ClientInfo> requester = nullptr;
    std::shared_ptr<ClientInfo> sbUser = nullptr;

    // 使用 for 循环查找请求者和黑名单用户
    for (auto& client : clients) {
        if (client->username == requesterUsername) {
            requester = client;
        }
        if (client->username == sbUsername) {
            sbUser = client;
        }
        // 一旦找到两者，退出循环
        if (requester && sbUser) break;
    }
    if (!requester) {
        // 请求者不存在
        return;
    }
    if (!sbUser) {
        // 黑名单用户不存在
        return;
    }
    // 将用户添加到请求者的黑名单
    requester->sbList.push_back(sbUsername);
}

void clientThread(SOCKET clientSocket, string ipAddress) {
    char buffer[BUFFER_SIZE];
    int bytesRead;

    bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (bytesRead <= 0) {
        cerr << "Failed to receive username." << endl;
        closesocket(clientSocket);
        return;
    }
    buffer[bytesRead] = '\0';
    string username(buffer);

    {
        lock_guard<mutex> lock(clientsMutex);
        clients.push_back(make_shared<ClientInfo>(clientSocket, username, ipAddress, true));
        userStatusMap[username] = true;
    }

    //cout << username << " (" << ipAddress << ") has joined the chat." << endl;
    logMessage(username + " (" + ipAddress + ") has joined the chat.", 10);
    printUserStatus();

    //broadcastMessage(username + " has joined the chat.", 10);

    while (true) {
        bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesRead <= 0) {
            {
                lock_guard<mutex> lock(clientsMutex);
                for (auto& client : clients) {
                    if (client->sockfd == clientSocket) {
                        client->isOnline = false;
                        cout << client->username << " has left the chat." << endl;
                        logMessage(client->username + " has left the chat.", 12);
                        userStatusMap[client->username] = false;
                        break;
                    }
                }
            }
            closesocket(clientSocket);
            printUserStatus();
            return;
        }
        buffer[bytesRead] = '\0';
        string message = sanitizeMessage(buffer);

        if (message == EXIT_COMMAND) {
            broadcastMessage(username + " has left the chat.", 12);
            logMessage(username + " has left the chat.", 12);
            closesocket(clientSocket);
            return;
        }
        if (message == HELP_COMMAND1) {
            string helpMessage = "Available commands:\n";
            helpMessage += "/help - Display this help message\n";
            helpMessage += "/upload <filepath> - Upload a file\n";
            helpMessage += "/pm <username> <message> - Send a private message\n";
            helpMessage += "/kick <username> - Kick a user from the chat\n";
            helpMessage += "/clear - Clear the chat screen\n";
            helpMessage += "/load - Load the chat log\n";
            helpMessage += "/status - Show user status\n";
            helpMessage += "/addfrd <username>- add a user to your friendlist\n";
            helpMessage += "/addsb <username>- add a user to your blacklist\n";
            helpMessage += "q - Quit the chat\n";
            send(clientSocket, helpMessage.c_str(), helpMessage.length(), 0);
            continue;
        }
        if (message.substr(0, UPLOAD_COMMAND.size()) == UPLOAD_COMMAND) {
            if (uploadFile(clientSocket)) {
                broadcastMessage(username + " uploaded a file.");
            }
            continue;
        }
        if (message.substr(0, PRIVATE_MESSAGE_COMMAND.size()) == PRIVATE_MESSAGE_COMMAND) {
            // 解析私聊命令
            string recipient, privateMessage;

            // 找到第一个空格
            size_t firstSpacePos = message.find(" ", PRIVATE_MESSAGE_COMMAND.size());
            if (firstSpacePos == string::npos) {
                send(clientSocket, "Invalid private message format. Usage: /pm <username> <message>\n", 64, 0);
                continue;
            }

            // 找到第二个空格
            size_t secondSpacePos = message.find(" ", firstSpacePos + 1);
            if (secondSpacePos != string::npos) {
                // 提取目标用户名
                recipient = message.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1);
                // 提取私聊消息
                privateMessage = message.substr(secondSpacePos + 1);
            }
            else {
                // 如果没有第二个空格，说明没有消息内容
                send(clientSocket, "Invalid private message format. Usage: /pm <username> <message>\n", 64, 0);
                continue;
            }

            // 去掉多余的空白
            recipient.erase(0, recipient.find_first_not_of(" \t\n")); // 去掉前导空格
            recipient.erase(recipient.find_last_not_of(" \t\n") + 1); // 去掉后导空格
            privateMessage.erase(0, privateMessage.find_first_not_of(" \t\n")); // 去掉前导空格

            // 查找目标用户并发送私聊信息
            lock_guard<mutex> lock(clientsMutex);
            bool recipientFound = false;
            for (const auto& client : clients) {
                if (client->username == recipient && client->isOnline) {
                    string formattedMessage = "[私聊] 来自 " + username + ": " + privateMessage;
                    send(client->sockfd, formattedMessage.c_str(), formattedMessage.length(), 0);  // 发送私聊信息
                    recipientFound = true;
                    break;
                }
            }

            if (!recipientFound) {
                string errorMsg = "用户 " + recipient + " 不在线或不存在。\n";
                send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
            }
            continue;
        }


        if (message.substr(0, KICK_COMMAND.size()) == KICK_COMMAND) {
            // Parse the kick command
            string targetUsername = message.substr(KICK_COMMAND.size() + 1);
            //cout <<"kick:" << targetUsername << endl;
            // Kick the user
            lock_guard<mutex> lock(clientsMutex);
            auto it = clients.begin(); // 使用迭代器遍历
            while (it != clients.end()) {
                if ((*it)->username == targetUsername) {
                    //cout << "yes" << endl;
                    //cout << "yes"<< (*it)->isOnline << endl;
                    if ((*it)->isOnline) {
                        (*it)->isOnline = false;
                        //broadcastMessage(targetUsername + " has been kicked from the chat by " + username, 12);
                        logMessage(targetUsername + " has been kicked from the chat by " + username, 12);
                        cout << targetUsername + " has been kicked from the chat by " + username << endl;
                        // Check if the socket is still valid before closing it
                        if ((*it)->sockfd != INVALID_SOCKET) {
                            if (closesocket((*it)->sockfd) == SOCKET_ERROR) {
                                logMessage("Failed to close socket for user " + targetUsername, 12);
                            }
                        }
                        /*cout << "yes1" << endl;*/
                        userStatusMap[targetUsername] = false;
                        //it = clients.erase(it);  // 将该用户从客户端列表中移除
                    }
                    else {
                        string errorMsg = "用户 " + targetUsername + " 已经离线。\n";
                        send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
                    }
                    break;
                }
                else {
                    ++it;
                }
            }
            continue;
        }
        if (message.substr(0, ADDFRDCOMMAND.size()) == ADDFRDCOMMAND) {
            string requesterUsername = getUsernameFromSocket(clientSocket);
            string friendUsername = message.substr(8);  // 获取好友用户名
            handleAddFriendRequest(requesterUsername, friendUsername);
            // 提示用户添加成功
            string formattedMessage = "User '" + friendUsername + "' has been added to your friendlist.";
            send(clientSocket, formattedMessage.c_str(), formattedMessage.length(), 0);
            // 调用 printFriendsList 并获取返回的消息
            std::string friendsListMessage = printFriendsList(requesterUsername);

            // 将好友列表发送给请求者
            send(clientSocket, friendsListMessage.c_str(), friendsListMessage.length(), 0);
            continue;
        }
        if (message.substr(0, ADDSBCOMMAND.size()) == ADDSBCOMMAND) {
            string requesterUsername = getUsernameFromSocket(clientSocket);
            string sbUsername = message.substr(7);  // 获取黑名单用户名
            handleAddSbRequest(requesterUsername, sbUsername);
            // 提示用户添加成功
            string formattedMessage = "User '" + sbUsername + "' has been added to your blacklist.";
            send(clientSocket, formattedMessage.c_str(), formattedMessage.length(), 0);
            std::string blackListMessage = printblackList(requesterUsername);

            // 将好友列表发送给请求者
            send(clientSocket, blackListMessage.c_str(), blackListMessage.length(), 0);
            continue;
        }
        if (message == SAVE_COMMAND) {
            // Save the chat log
            ofstream logFile("chat_log.txt", ios::app);
            auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
            struct tm timeInfo;
            localtime_s(&timeInfo, &now);
            logFile << put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << " - Chat log saved.\n";
            logFile.close();
            continue;
        }

        if (message.substr(0, LOAD_COMMAND.size()) == LOAD_COMMAND) {
            // Load the chat log and send it to the requesting client
            ifstream logFile("chat_log.txt");
            if (logFile.is_open()) {
                string line;
                while (getline(logFile, line)) {
                    send(clientSocket, line.c_str(), line.length(), 0);
                    send(clientSocket, "\n", 1, 0); // Send newline character
                }
                logFile.close();
            }
            else {
                string errorMessage = "Failed to open chat log file.\n";
                send(clientSocket, errorMessage.c_str(), errorMessage.length(), 0);
            }
            continue;
        }
        if (message == STATUS_COMMAND) {
            // Build the user status message and send it to the requesting client
            string statusMessage = "======= 在线用户列表 =======\n";
            lock_guard<mutex> lock(clientsMutex);
            for (const auto& client : clients) {
                statusMessage += client->username + (client->isOnline ? " (在线)\n" : " (离线)\n");
            }
            statusMessage += "============================\n";
            send(clientSocket, statusMessage.c_str(), statusMessage.length(), 0);
            continue;
        }

        time_t now = chrono::system_clock::to_time_t(chrono::system_clock::now());
        char buffer[26]; // ctime_s 需要的缓冲区大小
        ctime_s(buffer, sizeof(buffer), &now); // 使用 ctime_s 格式化时间

        string formattedMessage = "时间: " + string(buffer) + // 将时间格式化
            "用户: " + username + " (" + ipAddress + ")\n" + "said: " + message;
        broadcastMessage(formattedMessage, 14);
    }
}

void serverThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Failed to initialize Winsock." << endl;
        return;
    }
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Failed to create server socket." << endl;
        WSACleanup();
        return;
    }
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Failed to bind server socket." << endl;
        closesocket(serverSocket);
        WSACleanup();
        return;
    }
    if (listen(serverSocket, MAX_CLIENTS) == SOCKET_ERROR) {
        cerr << "Failed to listen on server socket." << endl;
        closesocket(serverSocket);
        WSACleanup();
        return;
    }
    cout << "Server is listening on port 8080." << endl;
    logMessage("Server started on port 8080.");
    while (true) {
        SOCKADDR_IN clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Failed to accept client connection." << endl;
            continue;
        }
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));

        // 创建新的客户端线程
        thread(clientThread, clientSocket, string(ipStr)).detach();
    }
    closesocket(serverSocket);
    WSACleanup();
}
int main() {
    // 清空 chat_log.txt 文件
    ofstream logFile("chat_log.txt", ios::trunc);
    if (!logFile) {
        cerr << "无法清空日志文件。" << endl;
        return 1;
    }
    logFile.close();  // 关闭文件
    printWelcomeScreen();
    thread serverThreadInstance(serverThread);
    serverThreadInstance.join();
    return 0;
}