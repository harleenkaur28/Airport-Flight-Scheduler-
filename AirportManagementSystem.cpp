#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

// Add to Flight class:
static constexpr int TIME_SCALE_FACTOR = 60; // 1 real second = 1 simulated minute
static constexpr int TAKEOFF_TIME = 300;       // 5 minutes in seconds
static constexpr int LANDING_TIME = 300;       // 5 minutes in seconds

class Flight
{
public:
    int id;
    std::string type;
    int priority;
    // std::chrono::system_clock::time_point scheduledTime;
    int timeInMinutes;
    std::string status;

    Flight(int id, const std::string &type, int priority, const std::string &timeStr)
        : id(id), type(type), priority(priority), status("waiting")
    {
        int hours, minutes;
        sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes);
        // std::tm tm = {};
        // std::istringstream ss(timeStr);
        // ss >> std::get_time(&tm, "%H:%M");
        // scheduledTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        timeInMinutes = hours * 60 + minutes;
    }
};

struct FlightComparator
{
    
    bool operator()(const Flight &f1, const Flight &f2) const
    {
        // First compare time slots (with some tolerance)
       int timeDiff = std::abs(f1.timeInMinutes - f2.timeInMinutes);
        std::cout << timeDiff <<  std::endl;
        // If flights are within same 5-minute slot, use priority
        if (std::abs(timeDiff) <= 5)
        {
            return f1.priority < f2.priority; // Higher priority first
        }

        // Otherwise, earlier time first
        return f1.timeInMinutes > f2.timeInMinutes;
    }
};

class Runway
{
public:
    int id;
    bool isAvailable;
    std::mutex mutex;
    std::condition_variable cv;

    // Default constructor
    Runway() : id(0), isAvailable(true) {}

    // Constructor with id
    explicit Runway(int runwayId) : id(runwayId), isAvailable(true) {}

    // Copy constructor
    Runway(const Runway &other) : id(other.id), isAvailable(other.isAvailable) {}

    // Move constructor
    Runway(Runway &&other) noexcept
        : id(other.id), isAvailable(other.isAvailable)
    {
        other.id = 0;
        other.isAvailable = false;
    }

    // Copy assignment operator
    Runway &operator=(const Runway &other)
    {
        if (this != &other)
        {
            id = other.id;
            isAvailable = other.isAvailable;
        }
        return *this;
    }

    // Move assignment operator
    Runway &operator=(Runway &&other) noexcept
    {
        if (this != &other)
        {
            id = other.id;
            isAvailable = other.isAvailable;
            other.id = 0;
            other.isAvailable = false;
        }
        return *this;
    }

    bool assignFlight(Flight &flight)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (isAvailable)
        {
            isAvailable = false;
            return true;
        }
        return false;
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        isAvailable = true;
        cv.notify_one();
    }
};
class AirportManager
{
private:
    std::vector<Runway> runways;
    std::priority_queue<Flight, std::vector<Flight>, FlightComparator> flightQueue;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    bool isShutdown;
    std::mutex outputMutex;

    // Helper function for formatted output
    void printMessage(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(outputMutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] "
                  << message << std::endl; // std::endl flushes the output
    }

    void processRunway(int runwayId)
    {
        while (true)
        {
            std::unique_lock<std::mutex> queueLock(queueMutex);

            auto now = std::chrono::system_clock::now();

            queueCV.wait(queueLock, [this, now]()
                         {
            if (flightQueue.empty()) return isShutdown;
            const Flight& nextFlight = flightQueue.top();
            // return isShutdown || nextFlight.timeInMinutes <= now; });
            return isShutdown;});

            if (isShutdown && flightQueue.empty())
            {
                printMessage("Runway " + std::to_string(runwayId) + " shutting down.");
                break;
            }

            if (!flightQueue.empty())
            {
                Flight flight = flightQueue.top();

                // if (flight.scheduledTime <= now || isShutdown)
                if (isShutdown)
                {
                    flightQueue.pop();
                    queueLock.unlock();

                    std::stringstream msg;
                    msg << "Runway " << runwayId << " processing "
                        << flight.type << " flight " << flight.id
                        << " (Priority: " << flight.priority << ")";
                    printMessage(msg.str());

                    // Simulate with scaled time
                    int processingTime = (flight.type == "takeoff" ? TAKEOFF_TIME : LANDING_TIME) / TIME_SCALE_FACTOR;

                    std::this_thread::sleep_for(
                        std::chrono::seconds(processingTime));

                    runways[runwayId - 1].release();

                    msg.str("");
                    msg << "Flight " << flight.id
                        << " completed " << flight.type
                        << " on runway " << runwayId
                        << " (Simulated " << (processingTime * TIME_SCALE_FACTOR)
                        << " seconds)";
                    printMessage(msg.str());
                }
            }
        }
    }

public:
    AirportManager(int numRunways) : isShutdown(false)
    {
        for (int i = 0; i < numRunways; ++i)
        {
            runways.emplace_back(i + 1);
        }
    }

    void addFlight(const Flight &flight)
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        flightQueue.push(flight);
        queueCV.notify_one();
    }

    void start()
    {
        std::vector<std::thread> runwayThreads;

        printMessage("Airport Management System starting...");
        printMessage("Number of active runways: " + std::to_string(runways.size()));

        for (size_t i = 0; i < runways.size(); ++i)
        {
            runwayThreads.emplace_back(&AirportManager::processRunway, this, i + 1);
        }

        printMessage("Press Enter to shutdown the airport system...");
        std::cin.get();

        // Initiate shutdown
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            isShutdown = true;
            queueCV.notify_all();
        }

        printMessage("Initiating shutdown sequence...");

        // Wait for all runway threads to complete
        for (auto &thread : runwayThreads)
        {
            thread.join();
        }

        printMessage("Airport Management System shutdown complete.");
    }
};

int main()
{
    int numRunways, numFlights;

    std::cout << "Enter number of runways: ";
    std::cin >> numRunways;
    std::cin.ignore();

    AirportManager airport(numRunways);

    std::cout << "Enter number of flights: ";
    std::cin >> numFlights;
    std::cin.ignore();

    for (int i = 0; i < numFlights; ++i)
    {
        int id, priority;
        std::string type, time;

        std::cout << "Enter flight details (ID Type[arrival/departure] Priority Time[HH:MM]): ";
        std::cin >> id >> type >> priority >> time;

        Flight flight(id, type, priority, time);
        airport.addFlight(flight);
    }

    airport.start();
    return 0;
}