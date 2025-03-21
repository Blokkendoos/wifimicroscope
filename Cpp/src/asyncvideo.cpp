#include "asyncvideo.h"

AsyncVideo::AsyncVideo(QWidget* parent) : running(true) {
    videoLabel = new QLabel(parent);
    QPushButton* btn = new QPushButton("Done", parent);
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->addWidget(videoLabel);
    layout->addWidget(btn);
    layout->setAlignment(videoLabel, Qt::AlignCenter);
    parent->setLayout(layout);

    QObject::connect(btn, &QPushButton::clicked, [this]() { endApplication(); });
    worker = std::thread(&AsyncVideo::workerThread, this);
    
    QTimer* timer = new QTimer(parent);
    //timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [this]() { updateGui(); });
    timer->start(50);
}

AsyncVideo::~AsyncVideo() {
    running = false;
    if (worker.joinable()) worker.join();
}

QImage mat2qimage(const cv::Mat& mat) {
    // Convert OpenCV image to Qt image
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage((const unsigned char*)(rgb.data),
                  rgb.cols, rgb.rows,
                  QImage::Format_RGB888);
}

void AsyncVideo::workerThread() {
    // Command (send) socket
    int cmdfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (cmdfd < 0) {
        std::cerr << "Cmd socket creation failed!" << std::endl;
        exit(EXIT_FAILURE);
    }
    sockaddr_in cmd_addr = {};
    cmd_addr.sin_family = AF_INET;
    cmd_addr.sin_addr.s_addr = inet_addr("192.168.29.1");
    cmd_addr.sin_port = htons(20000);
    if (connect(cmdfd, (struct sockaddr *)&cmd_addr, sizeof(cmd_addr)) < 0) {
        std::cerr << "Cmd socket connect failed!" << std::endl;
        exit(EXIT_FAILURE);
    }
    //const char* jhcmd_d0 = "JHCMD\xd0\x00";
    const char* jhcmd_10 = "JHCMD\x10\x00";
    const char* jhcmd_20 = "JHCMD\x20\x00";
    const char* heartbeat = "JHCMD\xd0\x01";
    //send(cmdfd, jhcmd_d0, strlen(jhcmd_d0), 0);
    send(cmdfd, jhcmd_10, strlen(jhcmd_10), 0);
    send(cmdfd, jhcmd_20, strlen(jhcmd_20), 0);
    send(cmdfd, heartbeat, strlen(heartbeat), 0);
    send(cmdfd, heartbeat, strlen(heartbeat), 0);
    // Stream (receive) socket
    int streamfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (streamfd < 0) {
        std::cerr << "Stream socket failed to create socket\n";
        exit(EXIT_FAILURE);
    }
    int flags = fcntl(streamfd, F_GETFL, 0);
    if (fcntl(streamfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Stream socket fcntl failed" << std::endl;
        close(streamfd);
        exit(EXIT_FAILURE);
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(10900);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(streamfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed" <<  std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout << "Listening on port " << addr.sin_port << "..." << std::endl;
    // WiFi loop
    std::vector<uint8_t> buffer;
    while (running) {
        // https://stackoverflow.com/questions/4223711/a-more-elegant-way-to-use-recv-and-vectorunsigned-char
        std::vector<uint8_t> packet(1450);
        ssize_t len = recv(streamfd, packet.data(), packet.size(), 0);
        if (len == 0) {
            std::cerr << "Connection broken..." <<  std::endl;
            exit(EXIT_FAILURE);
        }
        else if (len > 0) {
            packet.resize(len);
            int framenr = packet[0] + packet[1] * 256;
            int packetnr = packet[3];
            packet.erase(packet.begin(), packet.begin() + 8);  // strip header
            //DEBUG std::cout << "Packet " << packetnr << " length " << packet.size() << std::endl;
            if (packetnr == 0) {
                // A new frame has started
                if (framenr % 25 == 0) {
                    // Send heartbeat every x frames
                    send(cmdfd, heartbeat, strlen(heartbeat), 0);
                    //DEBUG std::cout << "Heartbeat " << framenr << std::endl;
                }
                if (buffer.size() > 8) {
                    // Convert JPEG to OpenCV Matrix
                    cv::Mat raw = cv::Mat(1, buffer.size(), CV_8UC1);
                    memcpy(raw.data, buffer.data(), buffer.size() * sizeof(uint8_t));
                    cv::Mat img = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
                    // Only process valid images
                    if (img.empty()) {
                        std::cout << "Invalid image" << " raw len " << buffer.size() <<  std::endl;
                    }
                    else {
                        cv::Mat rsz;
                        cv::resize(img, rsz, cv::Size(600, 400));
                        std::unique_lock<std::mutex> lock(queueMutex);
                        frameQueue.push(rsz);
                        lock.unlock();
                        queueCondVar.notify_one();
                    }
                }
                buffer.clear();
            }
            buffer.insert(buffer.end(), packet.begin(), packet.end());
        }
    }
    close(streamfd);
    close(cmdfd);
}

void AsyncVideo::updateGui() {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (!frameQueue.empty()) {
        cv::Mat frame = frameQueue.front();
        frameQueue.pop();
        lock.unlock();
        // OpenCV image to Qt image
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage qimg((const unsigned char*)(rgb.data),
                     rgb.cols, rgb.rows,
                     QImage::Format_RGB888);
        // NB function call results in unreliable images
        // (object going out of scope, or non thread-safe?)
        //videoLabel->setPixmap(QPixmap::fromImage(mat2qimage(frame)));
        videoLabel->setPixmap(QPixmap::fromImage(qimg));
    }
}

void AsyncVideo::endApplication() {
    std::cout << "Exit..." << std::endl;
    running = false;
    exit(1); // FIXME quit application graceful
}

