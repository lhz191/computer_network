#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>

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
#define BUFFER_SIZE 20480        // 数据缓冲区大小
#define TIMEOUT_MS 1000         // 超时时间（毫秒）
#define MAX_RETRIES 3           // 最大重试次数

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
void setConsoleColor(int color) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
#endif
}
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


bool checkheader(const PseudoHeader& pseudoHeader, const UDPHeader& udpHeader, const char* data, int dataLength) {
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
    sum += udpHeader.checksum;     // 校验和字段

    // 数据部分求和
    for (int i = 0; i < dataLength; i += 2) {
        uint16_t word = (data[i] << 8) + (i + 1 < dataLength ? data[i + 1] : 0);
        sum += word;
    }

    // 处理进位
    while (sum > 0xFFFF) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    // 检查结果
    if ((sum & 0xFFFF) == 0xFFFF) { // 所有位为 1 则校验正确
        cout << "校验正确" << endl;
        return true;
    }
    else {
        cout << "校验错误" << endl;
        return false;
    }
}

// 校验和计算函数
uint16_t calculateChecksum(const PseudoHeader& pseudoHeader, const UDPHeader& udpHeader, const char* data, int dataLength) {
    uint32_t sum = 0;
    // 伪首部求和
    sum += (pseudoHeader.srcIP >> 16) & 0xFFFF; // 源IP高16位
    sum += pseudoHeader.srcIP & 0xFFFF;        // 源IP低16位
    if (sum > 0xFFFF) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
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
    return ~static_cast<uint16_t>(sum);
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

bool performSecondWayHandshake(SOCKET serverSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, UDPHeader& header, const char* data, int dataLength, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber) {
    // 创建伪头部，伪头部的创建会使用服务端和客户端的地址信息
    PseudoHeader pseudoHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE + dataLength);
    header.acknowledgmentNumber = acknowledgmentNumber;
    // 创建完整的数据包
    vector<char> packet = createPacket(header, data, dataLength, pseudoHeader);

    // 三次握手：第二步，服务端发送SYN ACK
    setConsoleColor(10);
    cout << "尝试第二次握手建立连接..." << endl;
    setConsoleColor(7);
    int retries = 0;
    int wrongAckCount = 0; // 记录连续收到的错误ACK数量
    bool three_wrong = false;
    bool reach_time = false;
    bool handshakeComplete = false; // 用来标记握手是否完成

    while (retries < MAX_RETRIES && !handshakeComplete) {
        // 重置重试和错误ACK计数
        wrongAckCount = 0;
        three_wrong = false;
        reach_time = false;

        // 开始计时
        auto startTime = chrono::steady_clock::now();

        // 发送数据包
        if (sendto(serverSocket, packet.data(), packet.size(), 0, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
            setConsoleColor(12);
            cerr << "发送数据包失败!" << endl;
            setConsoleColor(7);
            return false;
        }
        cout << "数据发送成功，等待ACK...." << endl;
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
            FD_SET(serverSocket, &readfds);

            int result = select(0, &readfds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(serverSocket, &readfds)) {
                // 有数据到达
                int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);
                if (bytesReceived > 0) {
                    UDPHeader* recvHeader = (UDPHeader*)buffer;
                    cout << "实际收到的ACK号: " << recvHeader->acknowledgmentNumber << endl;
                    cout << "[服务端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    // 检查是否收到正确的ACK
                    if (recvHeader->flags == ACK_FLAG && recvHeader->acknowledgmentNumber == expectedAckNumber) {
                        setConsoleColor(10);
                        cout << "收到正确的ACK，第三次握手成功!" << endl;
                        setConsoleColor(7);
                        handshakeComplete = true; // 握手成功，退出循环
                        return true;
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
            else if (result == 0) {
                // 如果超时，进行重传
                setConsoleColor(12);
                cout << "数据传输超时，没有收到ACK，进行数据重传..." << endl;
                setConsoleColor(7);
                reach_time = true;
                retries++;
                if (retries >= MAX_RETRIES) {
                    setConsoleColor(12);
                    cerr << "达到最大重试次数，握手失败，无法建立连接。" << endl;
                    setConsoleColor(7);
                    closesocket(serverSocket);
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

    // 如果达到最大重试次数还是无法完成握手，则返回失败
    if (!handshakeComplete) {
        setConsoleColor(12);
        cerr << "达到最大重试次数，无法完成握手" << endl;
        setConsoleColor(7);
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    return true;
}

bool performSecondHandshake(SOCKET serverSocket, const sockaddr_in& clientAddr, const sockaddr_in& serverAddr, UDPHeader& header, const char* data, int dataLength, uint32_t& sequenceNumber, uint32_t& acknowledgmentNumber) {
    // 创建伪头部，伪头部的创建会使用服务端和客户端的地址信息
    PseudoHeader pseudoHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE + dataLength);
    header.acknowledgmentNumber = acknowledgmentNumber;
    // 创建完整的数据包
    vector<char> packet = createPacket(header, data, dataLength, pseudoHeader);

    // 三次握手：第二步，服务端发送FIN ACK
    setConsoleColor(10);
    cout << "尝试第二次挥手释放连接..." << endl;
    setConsoleColor(7);
    int retries = 0;
    int wrongAckCount = 0; // 记录连续收到的错误ACK数量
    bool three_wrong = false;
    bool reach_time = false;
    bool handshakeComplete = false; // 用来标记握手是否完成

    while (retries < MAX_RETRIES && !handshakeComplete) {
        // 重置重试和错误ACK计数
        wrongAckCount = 0;
        three_wrong = false;
        reach_time = false;

        // 开始计时
        auto startTime = chrono::steady_clock::now();

        // 发送数据包
        if (sendto(serverSocket, packet.data(), packet.size(), 0, (sockaddr*)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
            setConsoleColor(12);
            cerr << "发送数据包失败!" << endl;
            setConsoleColor(7);
            return false;
        }
        cout << "数据发送成功，等待ACK...." << endl;
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
            FD_SET(serverSocket, &readfds);

            int result = select(0, &readfds, nullptr, nullptr, &timeout);
            if (result > 0 && FD_ISSET(serverSocket, &readfds)) {
                // 有数据到达
                int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&recvAddr, &recvAddrLen);
                if (bytesReceived > 0) {
                    UDPHeader* recvHeader = (UDPHeader*)buffer;
                    cout << "实际收到的ACK号: " << recvHeader->acknowledgmentNumber << endl;
                    cout << "[服务端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    // 检查是否收到正确的ACK
                    if (recvHeader->flags == ACK_FLAG && recvHeader->acknowledgmentNumber == expectedAckNumber) {
                        setConsoleColor(10);
                        cout << "收到正确的ACK，第三次挥手成功!" << endl;
                        setConsoleColor(7);
                        handshakeComplete = true; // 握手成功，退出循环
                        return true;
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
            else if (result == 0) {
                // 如果超时，进行重传
                setConsoleColor(12);
                cout << "数据传输超时，没有收到ACK，进行数据重传..." << endl;
                setConsoleColor(7);
                reach_time = true;
                retries++;
                if (retries >= MAX_RETRIES) {
                    setConsoleColor(12);
                    cerr << "达到最大重试次数，握手失败，无法建立连接。" << endl;
                    setConsoleColor(7);
                    closesocket(serverSocket);
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

    // 如果达到最大重试次数还是无法完成握手，则返回失败
    if (!handshakeComplete) {
        setConsoleColor(12);
        cerr << "达到最大重试次数，无法完成握手" << endl;
        setConsoleColor(7);
        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    return true;
}


// 服务器实现
void udpServer(const char* clientIP, const char* serverIP, uint16_t serverPort, uint16_t clientPort) {
    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "初始化 Winsock 失败!" << endl;
        return;
    }

    // 创建UDP套接字
    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "套接字创建失败!" << endl;
        WSACleanup();
        return;
    }
    cout << "服务端套接字创建成功。" << endl;
    // 设置服务器地址
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);

    // 绑定套接字
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "绑定失败!" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return;
    }
    cout << "服务器已启动，等待连接..." << endl;
    // 等待客户端连接
    char buffer[BUFFER_SIZE] = {};
    sockaddr_in clientAddr = {};
    int clientAddrLen = sizeof(clientAddr);
    uint32_t sequenceNumber;
    uint32_t acknowledgmentNumber;
    //第一步：接收SYN
    int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);
    if (bytesReceived > 0) {
        UDPHeader* recvHeader = (UDPHeader*)buffer;
        if (recvHeader->flags == SYN_FLAG) {
            cout << "接收到SYN，准备发送SYN-ACK..." << endl;
            cout << "[来自客户端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
            sequenceNumber = 0;
            acknowledgmentNumber = (recvHeader->sequenceNumber) + 1;
            UDPHeader synHeader = createUDPHeader(serverPort, recvHeader->sourcePort, sequenceNumber, acknowledgmentNumber, SYN_ACK_FLAG, 0);
            // 发送数据包
            //PseudoHeader pseudosynHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE);
            //vector<char> synAckPacket = createPacket(synHeader, nullptr, 0, pseudosynHeader);
            //sendto(serverSocket, synAckPacket.data(), synAckPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
            //cout << "已发送SYN-ACK" << endl;
            if (performSecondWayHandshake(serverSocket, clientAddr, serverAddr, synHeader, nullptr, 0, sequenceNumber, acknowledgmentNumber) == 1)
            {
                setConsoleColor(10);
                cout << "成功建立连接" << endl;
                setConsoleColor(7);
            }
        }
    }
    ofstream outFile;
    int flag = 0;
    uint32_t wanted;
    while (1) {
        // 等待客户端结束连接
        char buffer_fin[BUFFER_SIZE] = {};
        sockaddr_in clientAddr_fin = {};
        int clientAddrLen_fin = sizeof(clientAddr);
        //第一步：接收FIN
        bytesReceived = recvfrom(serverSocket, buffer_fin, BUFFER_SIZE, 0, (sockaddr*)&clientAddr_fin, &clientAddrLen_fin);
        if (bytesReceived > 0) {
            UDPHeader* recvHeader = (UDPHeader*)buffer_fin;
            PseudoHeader pseudorecvHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, bytesReceived);
            if (recvHeader->flags == FIN_FLAG) {
                cout << "接收到FIN，准备发送FIN ACK..." << endl;
                cout << "[服务端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                acknowledgmentNumber = (recvHeader->sequenceNumber) + 1;
                UDPHeader synHeader = createUDPHeader(serverPort, recvHeader->sourcePort, ++sequenceNumber, acknowledgmentNumber, FIN_ACK_FLAG, 0);
                // 发送数据包
                //PseudoHeader pseudosynHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE);
                //vector<char> synAckPacket = createPacket(synHeader, nullptr, 0, pseudosynHeader);
                //sendto(serverSocket, synAckPacket.data(), synAckPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                //setConsoleColor(10);
                //cout << "已发送FIN ACK，成功释放连接" << endl;
                //setConsoleColor(7);
                //return;
                if (performSecondHandshake(serverSocket, clientAddr, serverAddr, synHeader, nullptr, 0, sequenceNumber, acknowledgmentNumber) == 1)
                {
                    setConsoleColor(10);
                    cout << "成功释放连接" << endl;
                    setConsoleColor(7);
                }

                // 关闭文件流，准备接收下一个用户的连接
                cout << "关闭文件流，准备接收下一个用户的连接" << endl;
                if (outFile.is_open()) {
                    outFile.close();
                }
                flag = 0;
                // 等待客户端连接
                char buffer[BUFFER_SIZE] = {};
                sockaddr_in clientAddr = {};
                int clientAddrLen = sizeof(clientAddr);
                uint32_t sequenceNumber;
                uint32_t acknowledgmentNumber;
                //第一步：接收SYN
                int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrLen);
                if (bytesReceived > 0) {
                    UDPHeader* recvHeader = (UDPHeader*)buffer;
                    if (recvHeader->flags == SYN_FLAG) {
                        setConsoleColor(10);
                        cout << "接收到SYN，准备发送SYN-ACK..." << endl;
                        setConsoleColor(7);
                        sequenceNumber = 0;
                        acknowledgmentNumber = (recvHeader->sequenceNumber) + 1;
                        UDPHeader synHeader = createUDPHeader(serverPort, recvHeader->sourcePort, sequenceNumber, acknowledgmentNumber, SYN_ACK_FLAG, 0);
                        // 发送数据包
                        //PseudoHeader pseudosynHeader = createPseudoHeader(clientAddr.sin_addr.s_addr, serverAddr.sin_addr.s_addr, HEADER_SIZE);
                        //vector<char> synAckPacket = createPacket(synHeader, nullptr, 0, pseudosynHeader);
                        //sendto(serverSocket, synAckPacket.data(), synAckPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                        //cout << "已发送SYN-ACK" << endl;
                        if (performSecondWayHandshake(serverSocket, clientAddr, serverAddr, synHeader, nullptr, 0, sequenceNumber, acknowledgmentNumber) == 1)
                        {
                            setConsoleColor(10);
                            cout << "成功建立连接" << endl;
                            setConsoleColor(7);
                        }
                    }
                }
                continue;
            }
            else if (recvHeader->flags == FILE_END)
            {
                setConsoleColor(10);
                cout << "接收到FILE_END，准备发送ACK..." << endl;
                cout << "[来自客户端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                setConsoleColor(7);
                acknowledgmentNumber = (recvHeader->sequenceNumber) + 1;
                UDPHeader synHeader = createUDPHeader(serverPort, recvHeader->sourcePort, ++sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                // 发送数据包
                PseudoHeader pseudosynHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE);
                vector<char> synAckPacket = createPacket(synHeader, nullptr, 0, pseudosynHeader);
                sendto(serverSocket, synAckPacket.data(), synAckPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                setConsoleColor(10);
                cout << "[服务端]发送数据包的序列号：" << synHeader.sequenceNumber << "，ACK号：" << synHeader.acknowledgmentNumber << "，校验和：" << synHeader.checksum << endl;
                cout << "已发送ACK，本次文件接收完毕" << endl;
                setConsoleColor(7);
                // 关闭文件流，准备接收下一个文件
                if (outFile.is_open()) {
                    outFile.close();
                }
                flag = 0;
                continue;
            }
            else
            {
                if (flag == 0)
                {
                    // 第一次收到数据包，数据包内容是文件名
                    string fileName(buffer_fin + HEADER_SIZE, bytesReceived - HEADER_SIZE);  // 提取文件名
                    cout << "接收到文件名: " << fileName << endl;
                    cout << "收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    // 创建文件路径，并打开文件
                    string filePath = "D:/new/" + fileName;  // 拼接文件路径
                    outFile.open(filePath, ios::binary);  // 使用文件路径创建文件
                    if (!outFile) {
                        cerr << "无法创建文件: " << filePath << endl;
                        return;
                    }
                    flag = 1;
                    char* data = buffer_fin + HEADER_SIZE;
                    int dataLength = bytesReceived - HEADER_SIZE;

                    if (checkheader(pseudorecvHeader, *recvHeader, data, bytesReceived - HEADER_SIZE) == true)
                    {
                        acknowledgmentNumber = recvHeader->sequenceNumber + 1;
                        UDPHeader ackHeader = createUDPHeader(serverPort, recvHeader->sourcePort, ++sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                        PseudoHeader ackPseudoHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE);
                        vector<char> ackPacket = createPacket(ackHeader, nullptr, 0, ackPseudoHeader);
                        sendto(serverSocket, ackPacket.data(), ackPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                        cout << "已发送ACK确认!" << endl;
                        wanted = ackHeader.acknowledgmentNumber;
                        cout << "发送数据包的序列号：" << ackHeader.sequenceNumber << "，ACK号：" << ackHeader.acknowledgmentNumber << "，校验和：" << ackHeader.checksum << endl;
                    }
                    //wanted = 0;
                    continue;
                }
                else
                {
                    cout << "==================进行数据接收=================" << endl;
                    cout << "[来自客户端]收到数据包的序列号：" << recvHeader->sequenceNumber << "，ACK号：" << recvHeader->acknowledgmentNumber << "，校验和：" << recvHeader->checksum << endl;
                    char* data = buffer_fin + HEADER_SIZE;
                    int dataLength = bytesReceived - HEADER_SIZE;
                    if (recvHeader->sequenceNumber == wanted && checkheader(pseudorecvHeader, *recvHeader, data, bytesReceived - HEADER_SIZE) == true)
                    {
                        // 保存到文件
                        outFile.write(data, dataLength);
                        cout << "已写入数据块: " << dataLength << " 字节" << endl;

                        acknowledgmentNumber = recvHeader->sequenceNumber + 1;
                        UDPHeader ackHeader = createUDPHeader(serverPort, recvHeader->sourcePort, ++sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                        PseudoHeader ackPseudoHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE);
                        vector<char> ackPacket = createPacket(ackHeader, nullptr, 0, ackPseudoHeader);
                        sendto(serverSocket, ackPacket.data(), ackPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                        cout << "已发送ACK确认!" << endl;
                        wanted = ackHeader.acknowledgmentNumber;
                        cout << "[服务端]发送数据包的序列号：" << ackHeader.sequenceNumber << "，ACK号：" << ackHeader.acknowledgmentNumber << "，校验和：" << ackHeader.checksum << endl;
                        cout << "===============================================" << endl;
                    }
                    else if (recvHeader->sequenceNumber != wanted || checkheader(pseudorecvHeader, *recvHeader, data, bytesReceived - HEADER_SIZE) == false)
                    {
                        acknowledgmentNumber = wanted;
                        UDPHeader ackHeader = createUDPHeader(serverPort, recvHeader->sourcePort, ++sequenceNumber, acknowledgmentNumber, ACK_FLAG, 0);
                        PseudoHeader ackPseudoHeader = createPseudoHeader(serverAddr.sin_addr.s_addr, clientAddr.sin_addr.s_addr, HEADER_SIZE);
                        vector<char> ackPacket = createPacket(ackHeader, nullptr, 0, ackPseudoHeader);
                        sendto(serverSocket, ackPacket.data(), ackPacket.size(), 0, (sockaddr*)&clientAddr, clientAddrLen);
                        cout << "已重新发送ACK确认!" << endl;
                        cout << "[服务端]发送数据包的序列号：" << ackHeader.sequenceNumber << "，ACK号：" << ackHeader.acknowledgmentNumber << "，校验和：" << ackHeader.checksum << endl;
                        cout << "===============================================" << endl;
                    }
                }
            }
        }
        else
        {
            cout << "无法建立连接" << endl;
            return;
        }
    }



    // 关闭套接字
    closesocket(serverSocket);
    WSACleanup();
    cout << "服务器已关闭!" << endl;
}

int main() {
    // 配置客户端和服务器信息
    const char* clientIP = "192.168.188.1"; // 客户端IP地址
    const char* serverIP = "192.168.188.1"; // 服务器IP地址
    uint16_t serverPort = 8888;              // 服务器端口
    //uint16_t clientPort = 8887;              // 客户端端口
    uint16_t clientPort = 8866;              // 客户端端口
    udpServer(clientIP, serverIP, serverPort, clientPort);
    return 0;
}