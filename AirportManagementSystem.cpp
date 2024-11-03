#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

std::condition_variable cv;

class Flight {
public:
    int id;
    std::string type; // "arrival" or "departure"
    int priority;
    std::string time;
    std::string status; // "waiting", "assigned", "landed"

    Flight(int id, const std::string& type, int priority, const std::string& time)
        : id(id), type(type), priority(priority), time(time), status("waiting") {}
};

class Runway {
public:
    int id;
    bool isAvailable;
    Flight* currentFlight;

    Runway(int id) : id(id), isAvailable(true), currentFlight(nullptr) {}

    // Delete copy constructor and copy assignment operator
    Runway(const Runway&) = delete;
    Runway& operator=(const Runway&) = delete;

    // Allow move constructor and move assignment
    Runway(Runway&& other) noexcept : id(other.id), isAvailable(other.isAvailable),
                                      currentFlight(other.currentFlight) {
        other.currentFlight = nullptr; // Invalidate the moved-from object
    }

    Runway& operator=(Runway&& other) noexcept {
        if (this != &other) {
            id = other.id;
            isAvailable = other.isAvailable;
            currentFlight = other.currentFlight;
            other.currentFlight = nullptr; // Invalidate the moved-from object
        }
        return *this;
    }

    bool assignFlight(Flight* flight) {
        std::lock_guard<std::mutex> lock(runwayMutex);
        if (isAvailable) {
            currentFlight = flight;
            isAvailable = false; // Mark the runway as occupied
            return true; // Flight assigned successfully
        }
        return false; // Runway is not available
    }

    void releaseAfterDelay(int seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        {
            std::lock_guard<std::mutex> lock(runwayMutex);
            isAvailable = true;
            currentFlight = nullptr;
        }
        cv.notify_one(); // Notify the waiting thread immediately
        std::cout << "Runway " << id << " is now available." << std::endl;
    }

private:
    std::mutex runwayMutex;
};

std::vector<Runway> runways;
std::mutex flightsMutex;

std::queue<Flight> preemptedFlights;
std::queue<Flight> regularFlights;

std::mutex runwayMutex;
std::condition_variable runwayAvailableCV;
void assignLanding(Flight& flight) {
    std::unique_lock<std::mutex> lock(runwayMutex);
    
    for (auto& runway : runways) {
        if (runway.isAvailable) {
            runway.isAvailable = false;
            std::cout << "Landing Flight ID: " << flight.id << " assigned to runway " << runway.id << "." << std::endl;
            lock.unlock();
            
            // Simulate landing time
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Mark runway as available
            lock.lock();
            runway.isAvailable = true;
            std::cout << "Runway " << runway.id << " is now available." << std::endl;
            
            // Notify checkWaitingFlights about the availability
            runwayAvailableCV.notify_one();
            break;
        }
    }
}


void checkWaitingFlights() {
    while (true) {
        std::unique_lock<std::mutex> lock(runwayMutex);
        
        // Wait for a runway to become available if all are occupied
        runwayAvailableCV.wait(lock, [] {
            for (const auto& runway : runways) {
                if (runway.isAvailable) return true;
            }
            return false;
        });

        // Check and assign any waiting flights
        if (!preemptedFlights.empty()) {
            Flight flight = preemptedFlights.front();
            preemptedFlights.pop();
            assignLanding(flight); // This will assign a free runway to the flight
        } else if (!regularFlights.empty()) {
            Flight flight = regularFlights.front();
            regularFlights.pop();
            assignLanding(flight);
        }

        // Break if no more flights are in the queues and all runways are free
        if (preemptedFlights.empty() && regularFlights.empty()) {
            bool allRunwaysFree = true;
            for (const auto& runway : runways) {
                if (!runway.isAvailable) {
                    allRunwaysFree = false;
                    break;
                }
            }
            if (allRunwaysFree) break;
        }
    }
}
int main() {
    int numRunways, numFlights;
    std::cout << "Enter the number of runways: ";
    std::cin >> numRunways;

    // Initialize the runways
    for (int i = 0; i < numRunways; ++i) {
        runways.emplace_back(i + 1); // Runway IDs start from 1
    }

    std::cout << "Enter the number of flights: ";
    std::cin >> numFlights;
    std::vector<Flight> flights;

    // Input flight details
    for (int i = 0; i < numFlights; ++i) {
        int id, priority;
        std::string type, time;
        std::cout << "Enter flight ID, type (arrival/departure), priority, and time: ";
        std::cin >> id >> type >> priority >> time;

        Flight flight(id, type, priority, time);
        flights.push_back(flight);
    }

    // Launch a thread to monitor and handle waiting flights
    std::thread monitorThread(checkWaitingFlights);

    // Process each flight based on type
    std::vector<std::thread> flightThreads; // Store threads for each flight to handle concurrency

    for (auto& flight : flights) {
        if (flight.type == "arrival") {
            // Assign landing using a thread for each flight
            flightThreads.emplace_back(assignLanding, std::ref(flight));
        } else if (flight.type == "departure") {
            // Placeholder for departure handling logic
            flightThreads.emplace_back([](Flight f) {
                std::cout << "Takeoff Flight ID: " << f.id << " assigned to runway (to be implemented)." << std::endl;
                // Departure handling logic can go here
            }, flight);
        }
    }

    // Wait for all flight assignment threads to finish
    for (auto& th : flightThreads) {
        if (th.joinable()) th.join();
    }

    // Signal the monitor thread to stop checking once all flights are processed
    monitorThread.join();

    // Check if all runways are available and queues are empty before exiting
    while (true) {
        bool allFree = true;
        for (const auto& runway : runways) {
            if (!runway.isAvailable) {
                allFree = false;
                break;
            }
        }

        if (preemptedFlights.empty() && regularFlights.empty() && allFree) {
            std::cout << "All flights have landed or taken off. Exiting system." << std::endl;
            break;
        }
    }

    return 0;
}