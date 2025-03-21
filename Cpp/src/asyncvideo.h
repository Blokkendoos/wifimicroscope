#include <opencv2/opencv.hpp>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <fcntl.h>

class AsyncVideo
{
public:
    AsyncVideo(QWidget* parent = Q_NULLPTR);
    ~AsyncVideo();

private slots:
    void updateGui();
    void endApplication();

private:
    void workerThread();
    
    std::queue<cv::Mat> frameQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondVar;

    std::atomic<bool> running;
    std::thread worker;
    QLabel* videoLabel;
};

