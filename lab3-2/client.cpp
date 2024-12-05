#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <limits> 
#include <string>
std::atomic<bool> ackReceived(false);  // 标志位，表示是否收到ACK
int ackNumber = -1;
int seqNumber = -1;
int flag = 0;
int finish = 0;
#pragma comment(lib, "ws2_32.lib")

using namespace std;

// 常量定义
#define HEADER_SIZE 20          // UDP头部长度（固定20字节）
#define PSEUDO_HEADER_SIZE 12   // 伪首部长度
#define SYN_FLAG 0x01           // SYN标志,SYN = 1 ACK = 0 FIN = 0
#define ACK_FLAG 0x02           // ACK标志，SYN = 0 ACK = 1 FIN = 0
#define SYN_ACK_FLAG 0x03           // ACK标志，SYN = 1 ACK = 1 FIN = 0
#define FIN_FLAG 0x04           // FIN标志，FIN = 1 ACK = 0 SYN = 0
#define FIN_ACK_FLAG 0x06       //FIN = 1 ACK = 1 SYN = 0 
#define FILE_END 0x07           //文件传输完毕标志
#define BUFFER_SIZE 10240       // 数据缓冲区大小
#define TIMEOUT_MS 100         // 超时时间（毫秒
//#define TIMEOUT_MS 1         // 超时时间（毫秒）
#define MAX_RETRIES 3           // 最大重试次数

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
    cout << "**********   欢迎进入文件传输系统   ************\n";
    cout << "************************************************\n\n";
    cout << "请输入消息或命令 ('q' 退出，'s'连接服务器):\n";
}

void setConsoleColor(int color) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
#endif
}
// 伪首部结构
struct PseudoHeader {
    uint32_t srcIP;            // 源IP
    uint32_t destIP;           // 目的IP
    uint8_t reserved;          // 填充位，固定为0
    uint8_t protocol;          // 协议号，UDP为17
    uint16_t udpLength;        // UDP头部+数据长度
    // 初始化
    PseudoHeader() : srcIP(0), destIP(0), reserved(0), protocol(17), udpLength(0) {}
};

// UDP头部结构
struct UDPHeader {
    uint16_t sourcePort;        // 源端口
    uint16_t destPort;          // 目的端口
    uint32_t sequenceNumber;    // 序列号
    uint32_t acknowledgmentNumber; // 确认号
    uint8_t flags;              // 标志位（ACK、SYN等）
    uint8_t reserved;           // 填充位，固定为0
    uint16_t length;            // UDP头部+数据长度
    uint16_t checksum;          // 校验和
    uint16_t windowSize;        // 窗口大小（固定为1）
    // 初始化
    UDPHeader() : sourcePort(0), destPort(0), sequenceNumber(0), acknowledgmentNumber(0),
        flags(0), reserved(0), length(0), checksum(0), windowSize(0) {}
};

// 校验和计算函数
uint16_t calculateChecksum(const PseudoHeader& pseudoHeader, const UDPHeader& udpHeader, const char* data, int dataLength) {
    uint32_t sum = 0;
    // 伪首部求和
    sum += (pseudoHeader.srcIP >> 16) & 0xFFFF; // 源IP高16位
    sum += pseudoHeader.srcIP & 0xFFFF;        // 源IP低16位
    sum += (pseudoHeader.destIP >> 16) & 0xFFFF; // 目的IP高16位
    sum += pseudoHeader.destIP & 0xFFFF;        // 目的IP低16位
    sum += (pseudoHeader.reserved << 8) + pseudoHeader.protocol; // reserved和protocol拼成16位
    sum += pseudoHeader.udpLength; // UDP长度

    // UDP头部求和
    sum += ntohs(udpHeader.sourcePort);  // 源端口
    sum += ntohs(udpHeader.destPort);    // 目的端口
    sum += (udpHeader.sequenceNumber >> 16) & 0xFFFF; // 序列号高16位
    sum += udpHeader.sequenceNumber & 0xFFFF;        // 序列号低16位
    sum += (udpHeader.acknowledgmentNumber >> 16) & 0xFFFF; // 确认号高16位
    sum += udpHeader.acknowledgmentNumber & 0xFFFF;        // 确认号低16位
    sum += (udpHeader.flags << 8) + udpHeader.reserved; // flags和reserved拼成16位
    sum += udpHeader.length;       // 长度字段
    sum += udpHeader.windowSize;   // 窗口大小
    sum += udpHeader.checksum;
    // 数据部分求和
    for (int i = 0; i < dataLength; i += 2) {
        uint16_t word = (data[i] << 8) + (i + 1 < dataLength ? data[i + 1] : 0);
        sum += word;
    }

    // 处理进位
    while (sum > 0xFFFF) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }


    // 取反，返回校验和
    uint16_t checksum = ~static_cast<uint16_t>(sum);
    //cout << "Final checksum: " << checksum << endl;
    return checksum;
}


// 构造伪首部
PseudoHeader createPseudoHeader(uint32_t srcIP, uint32_t destIP, uint16_t udpLength) {
    PseudoHeader pseudoHeader;
    pseudoHeader.srcIP = htonl(srcIP);       // 转换为网络字节序
    pseudoHeader.destIP = htonl(destIP);     // 转换为网络字节序
    pseudoHeader.reserved = 0;                // 填充位
    pseudoHeader.protocol = 17;               // UDP协议号
    pseudoHeader.udpLength = htons(udpLength); // UDP长度
    return pseudoHeader;
}

// 构造UDP头部
UDPHeader createUDPHeader(uint16_t sourcePort, uint16_t destPort, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber, uint8_t flags, uint16_t dataLength) {
    UDPHeader header;
    header.sourcePort = htons(sourcePort);
    header.destPort = htons(destPort);
    header.sequenceNumber = sequenceNumber;
    header.acknowledgmentNumber = acknowledgmentNumber;
    header.flags = flags; // 标志位
    header.reserved = 0;  // 填充位
    header.length = htons(HEADER_SIZE + dataLength);
    header.checksum = 0;  // 校验和初始为0
    header.windowSize = htons(1); // 窗口大小固定为1
    return header;
}

// 构造UDP数据包（包括伪首部、头部和数据）
vector<char> createPacket(UDPHeader& header, const char* data, int dataLength, const PseudoHeader& pseudoHeader) {
    // 拼接UDP头部和数据
    vector<char> packet(HEADER_SIZE + dataLength);
    //memcpy(packet.data(), &header, HEADER_SIZE); // 拷贝UDP头部
    //memcpy(packet.data() + HEADER_SIZE, data, dataLength); // 拷贝数据部分
    // 计算校验和
    header.checksum = calculateChecksum(pseudoHeader, header, data, dataLength);
    // 更新校验和到UDP头部
    memcpy(packet.data(), &header, HEADER_SIZE);
    memcpy(packet.data() + HEADER_SIZE, data, dataLength); // 拷贝数据部分
    return packet;
}
// 计算吞吐率函数
double calculateThroughput(long fileSize, chrono::duration<double> duration) {
    return static_cast<double>(fileSize) / duration.count() / 1024.0 / 1024.0; // MB/s
}

// 获取当前时间函数
chrono::steady_clock::time_point getCurrentTime() {
    return chrono::steady_clock::now();
}

// 发送数据包并等待ACK
bool sendDataAndWaitForAck(SOCKET clientSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, UDPHeader& header, const char* data, int dataLength) {
    PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE + dataLength);
    vector<char> packet = createPacket(header, data, dataLength, pseudoHeader);

    int retries = 0;
    int wrongAckCount = 0; // 记录连续收到的错误ACK数量
    bool three_wrong = false;
    bool reach_time = false;
    while (retries < MAX_RETRIES) {
        // 重置重试和错误ACK计数
        wrongAckCount = 0;
        three_wrong = false;
        reach_time = false;

        // 开始计时
        auto startTime = chrono::steady_clock::now();

        // 发送数据包
        if (sendto(clientSocket, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "发送数据包失败!" << endl;
            return false;
        }
        cout << "==================发送当前数据包===============" << endl;
        cout << "数据发送成功，等待ACK...." << endl;
        cout << "发送数据包的序列号：" << header.sequenceNumber << "，ACK号：" << header.acknowledgmentNumber << "，校验和：" << header.checksum << endl;
        uint32_t expectedAckNumber = header.sequenceNumber + 1;
        cout << "期望收到的ACK号: " << expectedAckNumber << endl;

        // 使用select进行超时判断
        fd_set readfds;
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS * 1000; // 设置超时时间

        while (true) {
            FD_ZERO(&readfds);
            FD_SET(clientSocket, &readfds);

            // 等待接收或超时
            int result = select(0, &readfds, nullptr, nullptr, &timeout);

            if (result > 0) {
                // 如果select返回大于0，表示有数据到达
                if (FD_ISSET(clientSocket, &readfds)) {
                    char buffer[BUFFER_SIZE] = {};
                    sockaddr_in recvAddr = {};
                    int recvAddrLen = sizeof(recvAddr);
                    int bytesReceived = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);

                    if (bytesReceived > 0) {
                        UDPHeader* recvHeader = (UDPHeader*)buffer;
                        cout << "实际收到的ACK号: " << recvHeader->acknowledgmentNumber << endl;
                        cout << "收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                        // 检查是否收到正确的ACK
                        if (recvHeader->flags == ACK_FLAG && recvHeader->acknowledgmentNumber == expectedAckNumber) {
                            cout << "收到正确的ACK，数据传输成功!" << endl;
                            seqNumber = recvHeader->sequenceNumber;
                            return true; // 收到正确的ACK，数据传输成功
                        }
                        else {
                            wrongAckCount++;
                            if (wrongAckCount >= 3) {
                                setConsoleColor(12);
                                cerr << "连续收到3个错误的ACK，准备重传数据包!" << endl;
                                cout << "进行数据重传..." << endl;
                                setConsoleColor(7);
                                three_wrong = true;
                                break; // 进入重传逻辑
                            }
                        }
                    }
                }
            }
            else if (result == 0) {
                // 如果select返回0，表示超时
                setConsoleColor(12);
                cout << "数据传输超时，没有收到ACK，进行数据重传..." << endl;
                setConsoleColor(7);
                reach_time = true;
                retries++;
                if (retries >= MAX_RETRIES) {
                    setConsoleColor(12);
                    cerr << "达到最大重试次数，数据传输失败，无法建立连接。" << endl;
                    setConsoleColor(7);
                    closesocket(clientSocket);
                    WSACleanup();
                    return false;
                }
                break; // 退出等待ACK的循环
            }

            // 如果遇到错误ACK或超时，重新发送数据包
            if (three_wrong || reach_time) {
                break;
            }
        }
    }
    return false; // 如果所有重试都失败，返回false
}



bool performThreeWayHandshake(SOCKET clientSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, UDPHeader& header, const char* data, int dataLength, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber) {

    PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE + dataLength);

    vector<char> packet = createPacket(header, data, dataLength, pseudoHeader);
    cout << "==============三次握手建立连接=================" << endl;
    // 三次握手：第一步，发送SYN
    setConsoleColor(10);  // 设置为绿色
    cout << "尝试第一次握手建立连接..." << endl;
    setConsoleColor(7);
    int retries = 0;
    int wrongAckCount = 0; // 记录连续收到的错误ACK数量
    bool three_wrong = false;
    bool reach_time = false;
    bool handshakeComplete = false; // 用来标记三次握手是否完成

    while (retries < MAX_RETRIES && !handshakeComplete) {
        // 重置重试和错误ACK计数
        wrongAckCount = 0;
        three_wrong = false;
        reach_time = false;

        // 开始计时
        auto startTime = chrono::steady_clock::now();

        // 发送数据包
        if (sendto(clientSocket, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            cerr << "发送数据包失败!" << endl;
            return false;
        }
        cout << "数据发送成功，等待SYN ACK...." << endl;
        cout << "发送数据包的序列号：" << header.sequenceNumber << "，ACK号：" << header.acknowledgmentNumber << "，校验和：" << header.checksum << endl;
        uint32_t expectedAckNumber = header.sequenceNumber + 1;
        cout << "期望收到的ACK号: " << expectedAckNumber << endl;

        // 等待ACK，持续超时检测
        while (!handshakeComplete) {
            char buffer[BUFFER_SIZE] = {};
            sockaddr_in recvAddr = {};
            int recvAddrLen = sizeof(recvAddr);

            // 使用select进行超时判断，持续监测ACK接收
            fd_set readfds;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = TIMEOUT_MS * 1000;
            FD_ZERO(&readfds);
            FD_SET(clientSocket, &readfds);

            int result = select(0, &readfds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(clientSocket, &readfds)) {
                // 有数据到达
                int bytesReceived = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);
                if (bytesReceived > 0) {
                    UDPHeader* recvHeader = (UDPHeader*)buffer;
                    cout << "实际收到的ACK号: " << recvHeader->acknowledgmentNumber << endl;
                    cout << "收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    // 检查是否收到正确的ACK
                    if (recvHeader->flags == SYN_ACK_FLAG && recvHeader->acknowledgmentNumber == expectedAckNumber) {
                        setConsoleColor(10);  // 设置为绿色
                        cout << "收到正确的SYN ACK，第二次握手成功!" << endl;
                        setConsoleColor(7);  // 设置为绿色
                        cout << "开始尝试进行第三次握手" << endl;
                        ++sequenceNumber;
                        acknowledgmentNumber = recvHeader->sequenceNumber + 1;
                        UDPHeader header_new = createUDPHeader(header.sourcePort, header.destPort, sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                        PseudoHeader pseudoHeader_new = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE);
                        vector<char> packet_new = createPacket(header_new, nullptr, 0, pseudoHeader_new);

                        // 输出第三次握手的序列号和确认号
                        cout << "第三次握手发送的序列号: " << header_new.sequenceNumber << "，ACK号: " << header_new.acknowledgmentNumber << "，校验和：" << header_new.checksum << endl;

                        if (sendto(clientSocket, packet_new.data(), packet_new.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                            cout << "第三次握手失败" << endl;
                            return false;
                        }
                        else {
                            setConsoleColor(10);
                            cout << "第三次握手成功，成功建立连接" << endl;
                            setConsoleColor(7);
                            handshakeComplete = true; // 握手成功，退出循环
                        }
                    }
                    else {
                        wrongAckCount++;
                        if (wrongAckCount >= 3) {
                            cerr << "连续收到3个错误的ACK，准备重传数据包!" << endl;
                            cout << "进行数据重传..." << endl;
                            three_wrong = true;
                            break; // 进入重传逻辑
                        }
                    }
                }
            }
            else if (result == 0) {
                // 如果超时，进行重传
                setConsoleColor(12);  // 设置为红色
                cout << "数据传输超时，没有收到ack，进行数据重传..." << endl;
                setConsoleColor(7);  // 设置为红色
                reach_time = true;
                retries++;
                if (retries >= MAX_RETRIES) {
                    setConsoleColor(12);  // 设置为红色
                    cerr << "达到最大重试次数，握手失败，服务器当前无法建立连接。" << endl;
                    setConsoleColor(7);
                    cout << "===============================================" << endl;
                    closesocket(clientSocket);
                    WSACleanup();
                    return false;
                }
                break; // 退出等待ACK的循环
            }
        }

        if (three_wrong || reach_time) {
            continue; // 超时或错误ACK后重试
        }
    }

    // 如果达到最大重试次数还是无法建立连接，则返回失败
    if (!handshakeComplete) {
        cerr << "达到最大重试次数，无法完成三次握手" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    return true;
}


bool performFourWayHandshake(SOCKET clientSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, UDPHeader& header, const char* data, int dataLength, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber) {

    PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE + dataLength);
    vector<char> packet = createPacket(header, data, dataLength, pseudoHeader);
    // 两次挥手：第一步，发送Fin
    cout << "尝试第一次挥手释放连接..." << endl;
    int retries = 0;
    int wrongAckCount = 0; // 记录连续收到的错误ACK数量
    bool three_wrong = false;
    bool reach_time = false;
    bool handshakeComplete = false; // 用来标记三次握手是否完成

    while (retries < MAX_RETRIES && !handshakeComplete) {
        // 重置重试和错误ACK计数
        wrongAckCount = 0;
        three_wrong = false;
        reach_time = false;

        // 开始计时
        auto startTime = chrono::steady_clock::now();

        // 发送数据包
        if (sendto(clientSocket, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            setConsoleColor(12);
            cerr << "发送数据包失败!" << endl;
            setConsoleColor(7);
            return false;
        }
        setConsoleColor(10);
        cout << "数据发送成功，等待FIN ACK...." << endl;
        cout << "发送数据包的序列号：" << header.sequenceNumber << "，ACK号：" << header.acknowledgmentNumber << "，校验和：" << header.checksum << endl;
        setConsoleColor(7);
        uint32_t expectedAckNumber = header.sequenceNumber + 1;
        cout << "期望收到的ACK号: " << expectedAckNumber << endl;

        // 等待ACK，持续超时检测
        while (!handshakeComplete) {
            char buffer[BUFFER_SIZE] = {};
            sockaddr_in recvAddr = {};
            int recvAddrLen = sizeof(recvAddr);

            // 使用select进行超时判断，持续监测ACK接收
            fd_set readfds;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = TIMEOUT_MS * 1000;
            FD_ZERO(&readfds);
            FD_SET(clientSocket, &readfds);

            int result = select(0, &readfds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(clientSocket, &readfds)) {
                // 有数据到达
                int bytesReceived = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);
                if (bytesReceived > 0) {
                    UDPHeader* recvHeader = (UDPHeader*)buffer;
                    cout << "实际收到的ACK号: " << recvHeader->acknowledgmentNumber << endl;
                    cout << "收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    // 检查是否收到正确的ACK
                    if (recvHeader->flags == FIN_ACK_FLAG && recvHeader->acknowledgmentNumber == expectedAckNumber) {
                        setConsoleColor(10);
                        cout << "收到正确的FIN ACK，第二次挥手成功!" << endl;
                        setConsoleColor(7);
                        /*return true;*/
                        cout << "开始尝试进行第三次挥手" << endl;
                        ++sequenceNumber;
                        acknowledgmentNumber = recvHeader->sequenceNumber + 1;
                        UDPHeader header_new = createUDPHeader(header.sourcePort, header.destPort, sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                        PseudoHeader pseudoHeader_new = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE);
                        vector<char> packet_new = createPacket(header_new, nullptr, 0, pseudoHeader_new);

                        // 输出第三次握手的序列号和确认号
                        cout << "第三次挥手发送的序列号: " << header_new.sequenceNumber << "，ACK号: " << header_new.acknowledgmentNumber << "，校验和：" << header_new.checksum << endl;

                        if (sendto(clientSocket, packet_new.data(), packet_new.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                            cout << "第三次挥手失败" << endl;
                            return false;
                        }
                        else {
                            setConsoleColor(10);
                            cout << "第三次挥手成功，成功释放连接" << endl;
                            setConsoleColor(7);
                            handshakeComplete = true; // 握手成功，退出循环
                        }
                    }
                }
            }
            else if (result == 0) {
                // 如果超时，进行重传
                setConsoleColor(12);
                cout << "数据传输超时，没有收到ack，进行数据重传..." << endl;
                setConsoleColor(7);
                reach_time = true;
                retries++;
                if (retries >= MAX_RETRIES) {
                    setConsoleColor(12);
                    cerr << "达到最大重试次数，握手失败，服务器当前无法建立连接。" << endl;
                    setConsoleColor(7);
                    closesocket(clientSocket);
                    WSACleanup();
                    return false;
                }
                break; // 退出等待ACK的循环
            }
        }

        if (three_wrong || reach_time) {
            continue; // 超时或错误ACK后重试
        }
    }

    // 如果达到最大重试次数还是无法建立连接，则返回失败
    if (!handshakeComplete) {
        setConsoleColor(12);
        cerr << "达到最大重试次数，无法完成三次握手" << endl;
        setConsoleColor(7);
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    return true;
}
#include <condition_variable>
#include <mutex>

std::mutex mtx;
std::condition_variable cv;
//bool ackReceived = false;  // 标志位
bool is_timeout = false;   // 超时标志

// 接收ACK的线程
void receiveAck(int clientSocket) {
    fd_set readfds;
    struct timeval timeout;
    while (finish == 0) {
        cout << "ack!!!!" << endl;
        char buffer[BUFFER_SIZE] = {};
        sockaddr_in recvAddr = {};
        int recvAddrLen = sizeof(recvAddr);
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS * 1000;
        FD_ZERO(&readfds);
        FD_SET(clientSocket, &readfds);
        int result = select(clientSocket + 1, &readfds, nullptr, nullptr, &timeout);
        if (result > 0) {
            int bytesReceived = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);
            if (bytesReceived > 0) {
                UDPHeader* recvHeader = (UDPHeader*)buffer;
                if (recvHeader->flags == ACK_FLAG) {
                    ackNumber = recvHeader->acknowledgmentNumber;
                    seqNumber = recvHeader->sequenceNumber;
                    std::cout << "收到 ACK：" << ackNumber << std::endl;
                    ackReceived = true;  // 收到ACK，设置标志位
                    //cv.notify_one(); // 通知发送线程
                }
            }
        }
        else {
            if (finish == 1) {
                return;
            }
            else {
                is_timeout = true;
                //cv.notify_one(); // 超时时通知发送线程
            }
        }
    }
    return;
}

#include <random>
#include <chrono>
#include <thread>

// 定义丢包率和延时
const int PACKET_DROP_RATE = 5; // 每 100 个包丢掉 PACKET_DROP_RATE 个
const int PACKET_DELAY_MS = 0;  // 每发一个包延时 PACKET_DELAY_MS 毫秒

bool sendData(int clientSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, std::vector<char>& fileContent, long fileSize, int size, uint16_t sourcePort, uint16_t destPort, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber) {
    int begin = 0;
    int middle = 0;
    int end = min((begin + size * BUFFER_SIZE - 1), (fileSize - 1));
    char data[BUFFER_SIZE];
    int ackCount = 0;
    uint32_t ackHistory[3] = { 0 };
    ackReceived = false;

    // 随机数生成器，用于丢包
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 100);

    while (begin < fileSize) {
        if (is_timeout) {
            setConsoleColor(12);
            cout << "触发超时重传机制" << endl;
            setConsoleColor(7);
            middle = begin;
            is_timeout = false;
        }

        if (ackReceived) {
            if ((ackNumber - 3) * BUFFER_SIZE < begin) {
                continue;
            }
            ackReceived = false;
            cout << "ackNumber:" << ackNumber << endl;
            begin = (ackNumber - 3) * BUFFER_SIZE;
            cout << "begin:" << begin << endl;

            if (begin >= fileSize) {
                cout << "所有数据发送完成！" << endl;
                finish = 1;
                return true;
            }

            end = min(begin + size * BUFFER_SIZE - 1, fileSize - 1);
            setConsoleColor(10);
            cout << "窗口调整，新的 begin: " << begin << ", end: " << end << endl;
            setConsoleColor(7);
            ackHistory[ackCount++] = ackNumber;
            if (ackCount >= 3) {
                ackCount -= 3;
            }
            if (ackHistory[0] == ackHistory[1] && ackHistory[1] == ackHistory[2] && ackHistory[0] != 0) {
                setConsoleColor(12);
                cout << "触发快速重传机制" << endl;
                setConsoleColor(7);
                middle = begin;
                ackHistory[0] = 0;
                ackHistory[1] = 0;
                ackHistory[2] = 0;
                ackCount = 0;
                continue;
            }
        }

        // 发送数据包
        if (middle < end) {
            if (end - middle + 1 < BUFFER_SIZE) {
                for (int i = middle; i <= end; i++) {
                    data[i - middle] = fileContent[i];
                }
                uint16_t bytesRead = end - middle + 1;
                sequenceNumber = middle / BUFFER_SIZE + 3;
                uint32_t ack = seqNumber + 1;
                UDPHeader dataHeader = createUDPHeader(sourcePort, destPort, sequenceNumber, ack, 0, bytesRead);
                PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE + bytesRead);
                vector<char> packet = createPacket(dataHeader, data, bytesRead, pseudoHeader);

                // 丢包判断
                if (dist(gen) > PACKET_DROP_RATE) {
                    if (sendto(clientSocket, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                        cerr << "发送数据包失败!" << endl;
                        return false;
                    }
                }
                else {
                    setConsoleColor(12);
                    cout << "数据包丢弃，序列号：" << sequenceNumber << endl;
                    setConsoleColor(7);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // 延时
                middle = end;
            }
            else {
                for (int i = middle; i <= middle + BUFFER_SIZE - 1; i++) {
                    data[i - middle] = fileContent[i];
                }
                uint16_t bytesRead = BUFFER_SIZE;
                sequenceNumber = middle / BUFFER_SIZE + 3;
                uint32_t ack = seqNumber + 1;
                UDPHeader dataHeader = createUDPHeader(sourcePort, destPort, sequenceNumber, ack, 0, bytesRead);
                PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE + bytesRead);
                vector<char> packet = createPacket(dataHeader, data, bytesRead, pseudoHeader);

                cout << "发送数据包的序列号：" << dataHeader.sequenceNumber << "，ACK号：" << dataHeader.acknowledgmentNumber << "，校验和：" << dataHeader.checksum << endl;

                // 丢包判断
                if (dist(gen) > PACKET_DROP_RATE) {
                    if (sendto(clientSocket, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                        cerr << "发送数据包失败!" << endl;
                        return false;
                    }
                }
                else {
                    setConsoleColor(12);
                    cout << "数据包丢弃，序列号：" << sequenceNumber << endl;
                    setConsoleColor(7);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(PACKET_DELAY_MS)); // 延时
                middle = middle + BUFFER_SIZE;
            }
        }

        // 等待ACK或超时
        //std::unique_lock<std::mutex> lock(mtx);
        //cv.wait_for(lock, std::chrono::milliseconds(TIMEOUT_MS), [] { return ackReceived || is_timeout; });
    }

    return true;
}

// 客户端实现
void udpClient(const char* clientIP, const char* serverIP, uint16_t serverPort, uint16_t clientPort, int size) {
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "初始化 Winsock 失败!" << endl;
        return;
    }

    // 创建UDP套接字
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "套接字创建失败!" << endl;
        WSACleanup();
        return;
    }
    setConsoleColor(10);
    cout << "客户端套接字创建成功。" << endl;
    setConsoleColor(7);
    // 设置客户端地址和端口
    sockaddr_in clientAddr = {};
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(clientPort);
    inet_pton(AF_INET, clientIP, &clientAddr.sin_addr); // 使用客户端IP
    if (bind(clientSocket, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
        cerr << "绑定失败!" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    // 设置服务器地址
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIP, &serverAddr.sin_addr); // 使用服务器IP



    uint32_t sequenceNumber = 0;
    uint32_t acknowledgmentNumber = 0;
    UDPHeader synHeader = createUDPHeader(clientPort, serverPort, sequenceNumber, acknowledgmentNumber, SYN_FLAG, 0);
    //PseudoHeader pseudoHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE);
    //vector<char> synPacket = createPacket(synHeader, nullptr, 0, pseudoHeader);

    if (performThreeWayHandshake(clientSocket, clientAddr, serverAddr, synHeader, nullptr, 0, sequenceNumber, acknowledgmentNumber) == 1)
    {
        cout << "成功建立连接" << endl;
        cout << "===============================================" << endl;
    }
    else
    {
        return;
    }
    uint32_t temp = sequenceNumber;
    uint32_t temp1;
    // 文件传输逻辑
    while (true) {
        finish = 0;
        sequenceNumber = temp;
        cout << "请输入要上传的文件路径（输入q释放连接）: ";
        string filePath;
        getline(cin, filePath);

        if (filePath == "q") {
            cout << "==============三次挥手释放连接=================" << endl;
            cout << "开始进行三次挥手，准备发送FIN包" << endl;
            UDPHeader finHeader = createUDPHeader(clientPort, serverPort, ++temp1, ++acknowledgmentNumber, FIN_FLAG, 0);
            if (performFourWayHandshake(clientSocket, clientAddr, serverAddr, finHeader, nullptr, 0, temp1, acknowledgmentNumber) == 1)
            {
                setConsoleColor(10);  // 设置为绿色
                cout << "成功释放连接" << endl;
                setConsoleColor(7);
            }

            // 关闭套接字
            closesocket(clientSocket);
            WSACleanup();
            cout << "连接已关闭!" << endl;
            cout << "===============================================" << endl;
            return;
        }

        ifstream file(filePath, ios::binary);
        if (!file) {
            cerr << "文件路径错误，无法打开文件!" << endl;
            continue;
        }
        // 获取文件大小
        file.seekg(0, ios::end);
        long fileSize = file.tellg();
        file.seekg(0, ios::beg);

        cout << "================传输文件名称===================" << endl;
        cout << "首先，传输文件名称....." << endl;
        // 提取文件名
        string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);  // 获取文件名（不含路径）
        std::cout << "传输的文件名为：" << fileName << std::endl;

        UDPHeader fileHeader = createUDPHeader(clientPort, serverPort, ++sequenceNumber, ++acknowledgmentNumber, 0, fileName.length());

        // 发送文件名
        if (sendDataAndWaitForAck(clientSocket, clientAddr, serverAddr, fileHeader, fileName.c_str(), fileName.length()) == true) {
            setConsoleColor(10);  // 设置为绿色
            cout << "文件名发送成功，开始传输文件内容..." << endl;
            setConsoleColor(7);
            cout << "===============================================" << endl;
        }
        else {
            setConsoleColor(12);
            cout << "无法与服务器建立连接，文件名传输失败!" << endl;
            setConsoleColor(7);
            cout << "===============================================" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return;
        }
        cout << "================进行文件传输===================" << endl;
        // 记录文件传输开始时间
        char data[BUFFER_SIZE];
        // 创建一个 vector，大小为文件的大小
        std::vector<char> fileContent(fileSize);
        // 读取文件内容到 vector 中
        file.read(fileContent.data(), fileSize); // fileContent.data() 返回指向 vector 数据的指针
        if (!file) {
            cout << "文件读取失败!" << endl;
            return;
        }
        cout << "文件读取成功，正在进行文件传输..." << endl;
        //std::thread sendThread(sendData, clientSocket,clientAddr,serverAddr,fileContent, fileSize, size,clientPort,serverPort,sequenceNumber,acknowledgmentNumber);  // 创建发送数据的线程
            // 创建并启动线程
        std::thread sendThread(sendData, clientSocket, std::cref(clientAddr), std::cref(serverAddr),
            std::ref(fileContent), fileSize, size, clientPort, serverPort,
            std::ref(sequenceNumber), std::ref(acknowledgmentNumber));

        std::thread receiveThread(receiveAck, clientSocket);  // 创建接收ACK的线程
        auto startTime = getCurrentTime();
        sendThread.join();  // 等待发送线程完成
        receiveThread.join();  // 等待接收线程完成

        auto endTime = getCurrentTime();
        file.close();
        setConsoleColor(10);
        cout << "文件传输完毕!" << endl;
        setConsoleColor(7);


        // 计算传输时间
        chrono::duration<double> transferDuration = endTime - startTime;

        // 计算吞吐率
        double throughput = calculateThroughput(fileSize, transferDuration);

        cout << "文件大小: " << fileSize / 1024.0 / 1024.0 << " MB" << endl;
        cout << "传输时间: " << transferDuration.count() << " 秒" << endl;
        cout << "吞吐率: " << throughput << " MB/s" << endl;


        cout << "===============================================" << endl;
        cout << "============发送文件传输完毕标志===============" << endl;
        cout << "开始发送文件传输完毕标志" << endl;
        UDPHeader dataHeader = createUDPHeader(clientPort, serverPort, ++sequenceNumber, ++acknowledgmentNumber, FILE_END, 0);
        if (sendDataAndWaitForAck(clientSocket, clientAddr, serverAddr, dataHeader, nullptr, 0) == true)
        {
            setConsoleColor(10);
            cout << "文件传输完毕标志传输完毕" << endl;
            setConsoleColor(7);
            temp1 = sequenceNumber;
            cout << "===============================================" << endl;
        }
        else
        {
            setConsoleColor(10);
            cout << "当前无法与服务器建立连接" << endl;
            // 关闭套接字
            closesocket(clientSocket);
            WSACleanup();
            cout << "连接已关闭!" << endl;
            setConsoleColor(7);
            temp1 = sequenceNumber;
            cout << "===============================================" << endl;
            return;
        }
        continue;
        //cout << "开始进行四次挥手，准备发送fin包" << endl;
        //UDPHeader finHeader = createUDPHeader(clientPort, serverPort, ++sequenceNumber, ++acknowledgmentNumber, FIN_FLAG, 0);
        //if (performFourWayHandshake(clientSocket, clientAddr, serverAddr, finHeader, nullptr, 0, sequenceNumber, acknowledgmentNumber) == 1)
        //{
        //    cout << "成功释放连接" << endl;
        //}

        //// 关闭套接字
        //closesocket(clientSocket);
        //WSACleanup();
        //cout << "连接已关闭!" << endl;
        //return;
    }

    // 关闭套接字
    closesocket(clientSocket);
    WSACleanup();
    cout << "连接已关闭!" << endl;
}
int main() {
    // 配置客户端和服务器信息
    const char* clientIP = "192.168.188.1"; // 客户端IP地址
    const char* serverIP = "192.168.188.1"; // 服务器IP地址
    //const char* serverIP = "127.0.0.1"; // 服务器IP地址
    //uint16_t serverPort = 8887;              // 服务器端口
    uint16_t serverPort = 8888;              // 服务器端口
    uint16_t clientPort = 8866;              // 客户端端口
    printWelcomeScreen();
    string s;
    getline(cin, s);
    if (s == "q")
    {
        return 0;
    }
    else if (s == "s") {
        // 启动客户端
        int size;
        cout << "请输入发送端窗口大小" << endl;
        cin >> size;
        // 忽略输入缓冲区中的换行符
        cin.ignore((numeric_limits<streamsize>::max)(), '\n');
        udpClient(clientIP, serverIP, serverPort, clientPort, size);

    }
    return 0;
}