// bench_main.cpp - measurement driver for Parallel Numerics Bench.
//
// Modes:
//   --mode scaling      timed sweep over (workload, size, threads); writes
//                       bench.csv (one row per run, including the discarded
//                       cold run, flagged in the `kept` column)
//   --mode progress     serial MC-pi run recorded at 60 checkpoints of
//                       1,000,000 samples each; writes progress.csv (the
//                       animation frames are these recorded checkpoints)
//   --mode equivalence  serial vs parallel output comparison sweep; writes
//                       equivalence.csv
//   --mode smoke        tiny end-to-end sanity run (used by CI), no files
//
// Options:
//   --out DIR        output directory (default: docs/results)
//   --repeats N      timed repeats per cell, default 8 (plus one cold run)
//   --threads LIST   comma-separated thread counts for scaling/equivalence,
//                    default 1,2,4,8,+hardware_concurrency (capped to it)
//   --keep-cold      include the cold run in the statistics
//
// The workloads stay 100% C++; this executable owns measurement, CSV
// recording and environment capture (environment.txt). Plotting happens in
// scripts/ from the recorded CSVs.
//
// License: MIT (see LICENSE).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "thread_pool.hpp"
#include "timing.hpp"
#include "workloads.hpp"

using namespace pnb;

namespace {

constexpr char kPiText[] = "3.14159265358979323846";

std::string compiler_string() {
#if defined(_MSC_VER)
    return std::string("MSVC ") + std::to_string(_MSC_VER);
#elif defined(__clang__) && defined(__VERSION__)
    return std::string("Clang ") + __VERSION__;
#elif defined(__GNUC__) && defined(__VERSION__)
    return std::string("GCC ") + __VERSION__;
#else
    return std::string("unknown compiler");
#endif
}

std::string build_type() {
#if defined(PNB_BUILD_TYPE)
    return PNB_BUILD_TYPE;
#else
    return "unspecified";
#endif
}

void write_environment(const std::string& out_dir) {
    std::ofstream env(out_dir + "/environment.txt");
    env << "compiler=" << compiler_string() << "\n";
    env << "build_type=" << build_type() << "\n";
    env << "cpp_standard=C++17 (command-line standard library only)\n";
    env << "hardware_concurrency=" << std::thread::hardware_concurrency() << "\n";
    env << "clock=std::chrono::steady_clock (monotonic)\n";
    env << "compile_date=" << __DATE__ << " " << __TIME__ << "\n";
    env << "pi_reference=" << kPiText << "\n";
}

std::vector<std::size_t> parse_threads(const std::string& list) {
    std::vector<std::size_t> out;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(static_cast<std::size_t>(std::stoull(item)));
        }
    }
    return out;
}

std::vector<std::size_t> default_thread_sweep() {
    const std::size_t hw = std::thread::hardware_concurrency();
    std::vector<std::size_t> sweep{1, 2, 4, 8};
    if (hw > 0) {
        sweep.push_back(hw);
    }
    std::sort(sweep.begin(), sweep.end());
    sweep.erase(std::unique(sweep.begin(), sweep.end()), sweep.end());
    sweep.erase(std::remove_if(sweep.begin(), sweep.end(),
                               [hw](std::size_t t) { return hw > 0 && t > hw; }),
                sweep.end());
    return sweep;
}

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path, const std::string& header) {
        file_.open(path);
        if (!file_) {
            throw std::runtime_error("cannot open output file: " + path);
        }
        file_ << header << "\n";
    }
    std::ofstream& stream() { return file_; }

private:
    std::ofstream file_;
};

}  // namespace

int main(int argc, char** argv) {
    std::string mode = "scaling";
    std::string out_dir = "docs/results";
    int repeats = 8;
    bool keep_cold = false;
    bool have_threads = false;
    std::string threads_arg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (a == "--repeats" && i + 1 < argc) {
            repeats = std::stoi(argv[++i]);
        } else if (a == "--threads" && i + 1 < argc) {
            threads_arg = argv[++i];
            have_threads = true;
        } else if (a == "--keep-cold") {
            keep_cold = true;
        } else {
            std::cerr << "unknown or incomplete option: " << a << "\n";
            return 2;
        }
    }
    if (repeats < 7) {
        // Methodology floor: at least 7 kept repeats are required.
        repeats = 7;
    }

    std::vector<std::size_t> sweep =
        have_threads ? parse_threads(threads_arg) : default_thread_sweep();
    if (sweep.empty()) {
        std::cerr << "empty thread sweep\n";
        return 2;
    }

    write_environment(out_dir);
    std::cout << "compiler: " << compiler_string() << " (" << build_type() << ")\n";
    std::cout << "hardware_concurrency: " << std::thread::hardware_concurrency() << "\n";
    std::cout << "mode: " << mode << ", repeats: " << repeats
              << (keep_cold ? " (cold kept)" : " (cold run discarded)") << "\n";

    if (mode == "progress") {
        constexpr std::size_t kCheckpoints = 60;
        constexpr std::size_t kChunk = 1000000;
        CsvWriter csv(out_dir + "/progress.csv",
                      "checkpoint,samples_cum,hits_cum,estimate,abs_err");
        std::uint64_t hits_cum = 0;
        const double pi = 3.14159265358979323846;
        for (std::size_t cp = 0; cp < kCheckpoints; ++cp) {
            const std::size_t first = cp * kChunk;
            // One stream per checkpoint block: stream = cp, counters within.
            hits_cum += mc_pi_block(first, first + kChunk, kChunk,
                                    static_cast<std::uint64_t>(cp));
            const std::size_t n = (cp + 1) * kChunk;
            const double est = 4.0 * static_cast<double>(hits_cum) /
                               static_cast<double>(n);
            csv.stream() << (cp + 1) << "," << n << "," << hits_cum << ","
                         << std::setprecision(17) << est << "," << std::setprecision(17)
                         << std::fabs(est - pi) << "\n";
            std::cout << "checkpoint " << (cp + 1) << "/" << kCheckpoints
                      << " samples=" << n << " estimate=" << est << "\n";
        }
        std::cout << "progress.csv written (" << kCheckpoints << " recorded checkpoints)\n";
        return 0;
    }

    if (mode == "smoke") {
        ThreadPool pool(2);
        // Same partitioning for both variants: 4 blocks of ceil(100003/4).
        const std::size_t blocks = 4;
        const std::size_t block_size = (100003 + blocks - 1) / blocks;
        const McPiResult a = mc_pi_serial(100003, block_size);
        const McPiResult b = mc_pi_parallel(pool, 100003, blocks);
        if (a.hits != b.hits) {
            std::cerr << "SMOKE FAIL: mc hits differ\n";
            return 1;
        }
        Matrix ma = random_matrix(33, 33, 1);
        Matrix mb = random_matrix(33, 33, 2);
        MatmulResult ca = matmul_serial(ma, mb);
        MatmulResult cb = matmul_parallel(pool, ma, mb, 5);
        if (ca.c.data != cb.c.data) {
            std::cerr << "SMOKE FAIL: matmul C differs\n";
            return 1;
        }
        std::cout << "SMOKE OK (mc hits=" << a.hits
                  << ", checksum delta=" << std::fabs(ca.checksum - cb.checksum)
                  << ")\n";
        return 0;
    }

    if (mode == "equivalence") {
        CsvWriter csv(out_dir + "/equivalence.csv",
                      "workload,size,threads,output_bit_identical,checksum_rel_delta,"
                      "parallel_rerun_bit_stable");
        const double pi = 3.14159265358979323846;

        // Monte Carlo pi: hits must match exactly for the same block size.
        const std::vector<std::size_t> mc_sizes{100003, 1000003, 8000009};
        for (std::size_t n : mc_sizes) {
            for (std::size_t t : sweep) {
                ThreadPool pool(t);
                const std::size_t blocks = t;
                const std::size_t block_size = (n + blocks - 1) / blocks;
                const McPiResult ser = mc_pi_serial(n, block_size);
                const McPiResult par = mc_pi_parallel(pool, n, blocks);
                const McPiResult again = mc_pi_parallel(pool, n, blocks);
                const bool ident = ser.hits == par.hits;
                const bool stable = par.estimate == again.estimate;
                const double rel = std::fabs(par.estimate - ser.estimate) / pi;
                csv.stream() << "mc_pi," << n << "," << t << "," << (ident ? 1 : 0)
                             << "," << std::setprecision(17) << rel << ","
                             << (stable ? 1 : 0) << "\n";
                std::cout << "mc_pi n=" << n << " t=" << t
                          << " identical=" << ident << " stable=" << stable << "\n";
            }
        }

        // Matmul: C bit-identical; checksum tolerance-bounded.
        const std::vector<std::size_t> mm_sizes{17, 96, 257};
        for (std::size_t s : mm_sizes) {
            Matrix a = random_matrix(s, s, 0x4142);
            Matrix b = random_matrix(s, s, 0x4344);
            const MatmulResult ser = matmul_serial(a, b);
            for (std::size_t t : sweep) {
                ThreadPool pool(t);
                const MatmulResult par = matmul_parallel(pool, a, b, t);
                const MatmulResult again = matmul_parallel(pool, a, b, t);
                const bool ident = par.c.data == ser.c.data;  // byte-exact compare
                const bool stable =
                    par.checksum == again.checksum && again.c.data == par.c.data;
                const double denom = std::fabs(ser.checksum) > 0.0
                                         ? std::fabs(ser.checksum)
                                         : 1.0;
                const double rel = std::fabs(par.checksum - ser.checksum) / denom;
                csv.stream() << "matmul," << s << "," << t << "," << (ident ? 1 : 0)
                             << "," << std::setprecision(17) << rel << ","
                             << (stable ? 1 : 0) << "\n";
                std::cout << "matmul n=" << s << " t=" << t
                          << " identical=" << ident << " stable=" << stable
                          << " checksum_rel_delta=" << rel << "\n";
            }
        }
        std::cout << "equivalence.csv written\n";
        return 0;
    }

    if (mode == "scaling") {
        CsvWriter csv(out_dir + "/bench.csv",
                      "workload,variant,size,threads,run,kept,time_s,result");
        const std::vector<std::size_t> mc_sizes{100000, 5000000};
        const std::vector<std::size_t> mm_sizes{96, 384, 768};

        // Measurement-order fairness: serial and parallel cells for the
        // same workload/size are INTERLEAVED round by round (round r runs
        // every cell once, serial cells included). Measuring all repeats of
        // one cell before the others lets CPU frequency drift and cache
        // state bias later cells; interleaving exposes every cell to the
        // same thermal/frequency history on average.
        struct Cell {
            int workload;        // 0 = mc_pi, 1 = matmul
            std::size_t size;
            std::size_t threads; // worker count for parallel; 1 for serial
            bool serial;
        };
        std::vector<Cell> cells;
        for (std::size_t n : mc_sizes) {
            for (std::size_t t : sweep) {
                cells.push_back({0, n, t, false});
            }
            cells.push_back({0, n, 1, true});
        }
        for (std::size_t s : mm_sizes) {
            for (std::size_t t : sweep) {
                cells.push_back({1, s, t, false});
            }
            cells.push_back({1, s, 1, true});
        }

        // Inputs built once per size, OUTSIDE all timed regions; identical
        // matrices for serial and parallel cells of the same size.
        std::vector<Matrix> mm_a;
        std::vector<Matrix> mm_b;
        for (std::size_t s : mm_sizes) {
            mm_a.push_back(random_matrix(s, s, 0x4142));
            mm_b.push_back(random_matrix(s, s, 0x4344));
        }

        for (int run = 0; run <= repeats; ++run) {
            for (const Cell& cell : cells) {
                double dt = 0.0;
                std::string result;
                if (cell.workload == 0) {
                    // MC serial block size matches the largest sweep entry
                    // so serial and parallel value maps stay comparable.
                    const std::size_t tmax = sweep.back();
                    const std::size_t block_size =
                        cell.serial ? (cell.size + tmax - 1) / tmax
                                    : (cell.size + cell.threads - 1) / cell.threads;
                    std::unique_ptr<ThreadPool> pool;
                    if (!cell.serial) {
                        pool = std::make_unique<ThreadPool>(cell.threads);
                    }
                    Stopwatch sw;
                    sw.start();
                    const McPiResult r =
                        cell.serial
                            ? mc_pi_serial(cell.size, block_size)
                            : mc_pi_parallel(*pool, cell.size, cell.threads);
                    dt = sw.stop_sec();
                    std::ostringstream os;
                    os << std::defaultfloat << std::setprecision(17) << r.estimate;
                    result = os.str();
                } else {
                    std::size_t idx = mm_sizes.size();
                    for (std::size_t i = 0; i < mm_sizes.size(); ++i) {
                        if (mm_sizes[i] == cell.size) {
                            idx = i;
                        }
                    }
                    std::unique_ptr<ThreadPool> pool;
                    if (!cell.serial) {
                        pool = std::make_unique<ThreadPool>(cell.threads);
                    }
                    Stopwatch sw;
                    sw.start();
                    const MatmulResult r =
                        cell.serial
                            ? matmul_serial(mm_a[idx], mm_b[idx])
                            : matmul_parallel(*pool, mm_a[idx], mm_b[idx],
                                              cell.threads);
                    dt = sw.stop_sec();
                    std::ostringstream os;
                    os << std::defaultfloat << std::setprecision(17) << r.checksum;
                    result = os.str();
                }
                const int kept = (run > 0 || keep_cold) ? 1 : 0;
                csv.stream() << (cell.workload == 0 ? "mc_pi" : "matmul") << ","
                             << (cell.serial ? "serial" : "parallel") << ","
                             << cell.size << "," << cell.threads << "," << run << ","
                             << kept << "," << std::fixed << std::setprecision(9)
                             << dt << "," << result << "\n";
            }
            std::cout << "round " << run << "/" << repeats << " done\n";
        }
        std::cout << "bench.csv written\n";
        return 0;
    }

    std::cerr << "unknown mode: " << mode << "\n";
    return 2;
}
