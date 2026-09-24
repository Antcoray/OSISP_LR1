// =====================================================================
// Лабораторная работа №1. Управление процессами, потоками и Fiber.
// Вариант 12: Исследование приоритетов потоков.
//
// Три вычислительных потока с приоритетами
//   THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL,
//   THREAD_PRIORITY_ABOVE_NORMAL
// выполняют ОДИНАКОВЫЙ объём вычислений (подсчёт простых чисел
// в одном и том же диапазоне методом пробного деления).
// Для каждого потока фиксируются: TID, приоритет, число найденных
// простых чисел (для контроля корректности) и время выполнения.
// Эксперимент повторяется NUM_RUNS (>=5) раз.
// =====================================================================

#include <windows.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

// ---------------------------------------------------------------------
// Вывод информации об ошибке Windows API: имя операции, код GetLastError,
// текстовое описание ошибки.
// ---------------------------------------------------------------------
static void ReportError(const char* operation)
{
    DWORD err = GetLastError();
    LPSTR buf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buf, 0, nullptr);

    std::cerr << "[ОШИБКА] Операция: " << operation
               << " | GetLastError() = " << err
               << " | Описание: " << (buf ? buf : "нет описания");
    if (buf) LocalFree(buf);
}

// ---------------------------------------------------------------------
// Вычислительная нагрузка: подсчёт количества простых чисел в диапазоне
// [rangeStart, rangeEnd) методом пробного деления. Специально выбран
// алгоритм с заведомо большой и предсказуемой вычислительной сложностью,
// не устраняемый оптимизатором (результат используется и проверяется).
// ---------------------------------------------------------------------
static unsigned long long CountPrimesInRange(unsigned long long rangeStart, unsigned long long rangeEnd)
{
    unsigned long long count = 0;
    for (unsigned long long n = rangeStart; n < rangeEnd; ++n)
    {
        if (n < 2) continue;
        bool isPrime = true;
        for (unsigned long long d = 2; d * d <= n; ++d)
        {
            if (n % d == 0) { isPrime = false; break; }
        }
        if (isPrime) ++count;
    }
    return count;
}

static double ElapsedMs(const LARGE_INTEGER& t0, const LARGE_INTEGER& t1, const LARGE_INTEGER& freq)
{
    return static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

// ---------------------------------------------------------------------
// Параметры и результат работы одного потока.
// ---------------------------------------------------------------------
struct ThreadContext
{
    // входные параметры
    int         workerIndex;      // номер рабочего потока (1..3)
    int         priority;         // константа приоритета Windows
    const char* priorityName;     // текстовое имя приоритета
    unsigned long long rangeStart;
    unsigned long long rangeEnd;
    HANDLE      startEvent;       // общий сигнал одновременного старта

    // результат
    DWORD  tid;
    unsigned long long primesFound;
    double elapsedMs;
};

static DWORD WINAPI WorkerProc(LPVOID param)
{
    ThreadContext* ctx = static_cast<ThreadContext*>(param);

    // Ждём общего сигнала, чтобы все три потока стартовали одновременно
    // и приоритет мог реально повлиять на распределение процессорного времени.
    if (WaitForSingleObject(ctx->startEvent, INFINITE) == WAIT_FAILED)
    {
        ReportError("WaitForSingleObject(startEvent) в потоке");
        return 1;
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    unsigned long long primes = CountPrimesInRange(ctx->rangeStart, ctx->rangeEnd);

    QueryPerformanceCounter(&t1);

    ctx->primesFound = primes;
    ctx->elapsedMs   = ElapsedMs(t0, t1, freq);
    return 0;
}

// ---------------------------------------------------------------------
// Проверка входных данных командной строки.
// ---------------------------------------------------------------------
static bool ParseArgs(int argc, char** argv, unsigned long long& rangeSize, bool& singleCore, int& numRuns, bool& pauseBeforeStart)
{
    rangeSize  = 2000000ULL; // диапазон [2, rangeSize) для подсчёта простых чисел
    singleCore = true;       // по умолчанию — принудительно 1 ядро (чтобы приоритет был заметен)
    numRuns    = 5;
    pauseBeforeStart = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--multicore")      { singleCore = false; continue; }
        if (arg == "--singlecore")     { singleCore = true;  continue; }
        if (arg == "--pause")          { pauseBeforeStart = true; continue; }

        if (arg.rfind("--runs=", 0) == 0)
        {
            try
            {
                long v = std::stol(arg.substr(7));
                if (v < 1 || v > 50) throw std::invalid_argument("out of range");
                numRuns = static_cast<int>(v);
            }
            catch (...)
            {
                std::cerr << "Некорректное значение --runs (ожидается целое число 1..50): " << arg << "\n";
                return false;
            }
            continue;
        }

        // числовой аргумент без флага — размер диапазона
        try
        {
            long long v = std::stoll(arg);
            if (v < 1000 || v > 200000000LL)
                throw std::invalid_argument("out of range");
            rangeSize = static_cast<unsigned long long>(v);
        }
        catch (...)
        {
            std::cerr << "Некорректный аргумент: \"" << arg << "\".\n"
                      << "Использование: lab1.exe [размер_диапазона] [--singlecore|--multicore] [--runs=N] [--pause]\n"
                      << "  размер_диапазона : целое число 1000..200000000 (по умолчанию 2000000)\n"
                      << "  --pause          : пауза перед стартом потоков (для демонстрации приоритетов в Диспетчере задач)\n";
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "=== Лабораторная работа №1. Вариант 12: приоритеты потоков ===\n\n";

    unsigned long long rangeSize;
    bool singleCore;
    int numRuns;
    bool pauseBeforeStart;
    if (!ParseArgs(argc, argv, rangeSize, singleCore, numRuns, pauseBeforeStart))
        return 1;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    std::cout << "Логических процессоров в системе : " << sysInfo.dwNumberOfProcessors << "\n";
    std::cout << "Диапазон подсчёта простых чисел   : [2, " << rangeSize << ")\n";
    std::cout << "Режим CPU-affinity                : " << (singleCore ? "1 ядро (принудительно)" : "по умолчанию (все ядра)") << "\n";
    std::cout << "Число повторов эксперимента       : " << numRuns << "\n\n";

    // Принудительно ограничиваем процесс одним логическим ядром, чтобы три
    // потока реально конкурировали за процессорное время и разница
    // приоритетов была наблюдаема. В режиме --multicore ограничение снимается,
    // и потоки, как правило, выполняются параллельно на разных ядрах примерно
    // за одинаковое время — это тоже фиксируется и объясняется в отчёте.
    if (singleCore)
    {
        DWORD_PTR mask = 1; // ядро №0
        if (!SetProcessAffinityMask(GetCurrentProcess(), mask))
        {
            ReportError("SetProcessAffinityMask");
            std::cerr << "Продолжаем без ограничения affinity.\n";
        }
    }

    // --- Эталонное (последовательное) вычисление для проверки корректности ---
    std::cout << "Выполняется контрольное последовательное вычисление...\n";
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    unsigned long long referencePrimes = CountPrimesInRange(2, rangeSize);
    QueryPerformanceCounter(&t1);
    double referenceMs = ElapsedMs(t0, t1, freq);
    std::cout << "Эталонный результат: простых чисел = " << referencePrimes
               << ", время = " << std::fixed << std::setprecision(2) << referenceMs << " мс\n\n";

    struct PriorityDef { int value; const char* name; };
    const PriorityDef priorities[3] = {
        { THREAD_PRIORITY_BELOW_NORMAL, "BELOW_NORMAL" },
        { THREAD_PRIORITY_NORMAL,       "NORMAL"       },
        { THREAD_PRIORITY_ABOVE_NORMAL, "ABOVE_NORMAL" },
    };

    // Результаты времени по приоритетам для итоговой статистики (мс), по прогонам.
    std::vector<double> timesByPriority[3];

    bool overallCorrect = true;

    for (int run = 1; run <= numRuns; ++run)
    {
        std::cout << "----- Прогон " << run << " из " << numRuns << " -----\n";

        HANDLE startEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr); // manual-reset, non-signaled
        if (startEvent == nullptr)
        {
            ReportError("CreateEvent");
            return 1;
        }

        ThreadContext ctx[3];
        HANDLE handles[3] = { nullptr, nullptr, nullptr };
        bool createFailed = false;

        for (int i = 0; i < 3 && !createFailed; ++i)
        {
            ctx[i].workerIndex   = i + 1;
            ctx[i].priority      = priorities[i].value;
            ctx[i].priorityName  = priorities[i].name;
            ctx[i].rangeStart    = 2;
            ctx[i].rangeEnd      = rangeSize; // все потоки выполняют ОДИНАКОВЫЙ объём вычислений
            ctx[i].startEvent    = startEvent;
            ctx[i].tid           = 0;
            ctx[i].primesFound   = 0;
            ctx[i].elapsedMs     = 0.0;

            DWORD threadId = 0;
            handles[i] = CreateThread(nullptr, 0, WorkerProc, &ctx[i], 0, &threadId);
            if (handles[i] == nullptr)
            {
                ReportError("CreateThread");
                createFailed = true;
                break;
            }
            ctx[i].tid = threadId; // получаем TID сразу от CreateThread — без гонки с самим потоком

            if (!SetThreadPriority(handles[i], ctx[i].priority))
            {
                ReportError("SetThreadPriority");
                createFailed = true;
                break;
            }

            int actualPriority = GetThreadPriority(handles[i]);
            if (actualPriority == THREAD_PRIORITY_ERROR_RETURN)
            {
                ReportError("GetThreadPriority");
                createFailed = true;
                break;
            }
        }

        if (createFailed)
        {
            for (int i = 0; i < 3; ++i)
                if (handles[i]) { TerminateThread(handles[i], 1); CloseHandle(handles[i]); }
            CloseHandle(startEvent);
            return 1;
        }

        // Демонстрационная пауза: потоки уже созданы и приоритеты назначены,
        // но ещё ждут startEvent. Даёт время открыть Диспетчер задач/Process
        // Explorer и убедиться, что приоритеты у потоков разные, ДО того как
        // начнутся вычисления.
        if (pauseBeforeStart)
        {
            std::cout << "Потоки созданы, приоритеты установлены:\n";
            for (int i = 0; i < 3; ++i)
                std::cout << "  Поток " << ctx[i].workerIndex << ": TID=" << ctx[i].tid
                           << ", приоритет=" << ctx[i].priorityName << "\n";
            std::cout << "Нажмите Enter, чтобы запустить вычисления...";
            std::cin.get();
        }

        // Одновременный старт всех трёх потоков.
        if (!SetEvent(startEvent))
        {
            ReportError("SetEvent");
        }

        DWORD waitResult = WaitForMultipleObjects(3, handles, TRUE, INFINITE);
        if (waitResult == WAIT_FAILED)
        {
            ReportError("WaitForMultipleObjects");
        }

        // Примечание: заголовки и значения таблицы намеренно на латинице —
        // std::setw считает БАЙТЫ, а не символы; кириллица в UTF-8 занимает
        // по 2 байта на символ, из-за чего колонки съезжаются.
        std::cout << std::left
                   << std::setw(10) << "Worker"
                   << std::setw(8)  << "TID"
                   << std::setw(15) << "Priority"
                   << std::setw(14) << "PrimesFound"
                   << std::setw(12) << "Time_ms"
                   << std::setw(10) << "State"
                   << "ExitCode\n";

        for (int i = 0; i < 3; ++i)
        {
            DWORD exitCode = 0;
            if (!GetExitCodeThread(handles[i], &exitCode))
            {
                ReportError("GetExitCodeThread");
                exitCode = static_cast<DWORD>(-1);
            }

            bool correct = (ctx[i].primesFound == referencePrimes) && (exitCode == 0);
            overallCorrect = overallCorrect && correct;

            std::cout << std::left
                       << std::setw(10) << ctx[i].workerIndex
                       << std::setw(8)  << ctx[i].tid
                       << std::setw(15) << ctx[i].priorityName
                       << std::setw(14) << ctx[i].primesFound
                       << std::setw(12) << std::fixed << std::setprecision(2) << ctx[i].elapsedMs
                       << std::setw(10) << "finished"
                       << exitCode
                       << (correct ? "" : "   <-- MISMATCH!")
                       << "\n";

            timesByPriority[i].push_back(ctx[i].elapsedMs);

            if (!CloseHandle(handles[i]))
                ReportError("CloseHandle(thread)");
        }

        if (!CloseHandle(startEvent))
            ReportError("CloseHandle(startEvent)");

        std::cout << "\n";
    }

    // --- Итоговая статистика по приоритетам ---
    std::cout << "===== Итоги по " << numRuns << " прогонам =====\n";
    std::cout << std::left
               << std::setw(15) << "Priority"
               << std::setw(12) << "Min_ms"
               << std::setw(12) << "Max_ms"
               << std::setw(12) << "Avg_ms" << "\n";

    for (int i = 0; i < 3; ++i)
    {
        double sum = 0, mn = timesByPriority[i][0], mx = timesByPriority[i][0];
        for (double v : timesByPriority[i]) { sum += v; mn = min(mn, v); mx = max(mx, v); }
        double avg = sum / timesByPriority[i].size();

        std::cout << std::left
                   << std::setw(15) << priorities[i].name
                   << std::setw(12) << std::fixed << std::setprecision(2) << mn
                   << std::setw(12) << mx
                   << std::setw(12) << avg
                   << "\n";
    }

    std::cout << "\nПроверка корректности (сравнение с последовательным эталоном): "
               << (overallCorrect ? "ПРОЙДЕНА (все результаты совпали)" : "ОШИБКА (есть расхождения!)") << "\n";

    return overallCorrect ? 0 : 2;
}
