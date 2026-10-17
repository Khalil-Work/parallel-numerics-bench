# API reference

Everything lives in namespace `pnb`, header-only, C++17, standard library
only (`<thread>`, `<future>`, `<mutex>`, `<condition_variable>`, ...).
Include directory: `src/`.

## ThreadPool - `src/thread_pool.hpp`

```cpp
class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads);       // throws if threads == 0
    ~ThreadPool();                                  // drains queue, joins
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    std::size_t worker_count() const noexcept;
    std::size_t queued_tasks() const;   // snapshot, excludes running tasks
};
```

* Fixed set of workers started in the constructor; condition-variable
  worker queue; tasks wrapped in `std::packaged_task` (shared_ptr shim for
  the move-only-in-`std::function` problem), so `enqueue` returns a future
  carrying the value OR the exception the callable threw.
* The destructor stops accepting work, DRAINS the queued tasks (futures
  already handed out become ready) and joins the workers.
* `enqueue` throws `std::runtime_error` once shutdown has begun. Calling
  `enqueue` on a destroyed pool is undefined (the usual textbook contract).
* Execution order across workers is unspecified by design; the workloads
  are written so this cannot affect results (see `docs/model.md`).

## Counter RNG - `src/counter_rng.hpp`

```cpp
std::uint64_t counter_sample(std::uint64_t stream, std::uint64_t counter);
double        counter_unit(std::uint64_t stream, std::uint64_t counter); // [0,1)

class CounterRng {                                    // sequential cursor
public:
    explicit CounterRng(std::uint64_t stream, std::uint64_t start_counter = 0);
    std::uint64_t next();
    double next_unit();
    std::uint64_t stream()  const noexcept;
    std::uint64_t counter() const noexcept;
};

std::uint64_t raw_counter_sample(std::uint64_t stream, std::uint64_t counter);
// negative control for test-power validation (not a generator!)
```

`counter_sample` is a pure function: identical `(stream, counter)` pairs
give identical 64-bit outputs on every machine and every run.

## Workloads - `src/workloads.hpp`

Partitioning helper (pure):

```cpp
std::size_t block_begin(std::size_t n, std::size_t blocks, std::size_t b); // n*b/blocks
std::size_t block_end  (std::size_t n, std::size_t blocks, std::size_t b); // n*(b+1)/blocks
```

Monte Carlo pi:

```cpp
std::uint64_t mc_pi_block(std::size_t first, std::size_t last,
                          std::size_t block_size, std::uint64_t stream_offset);

struct McPiResult { std::uint64_t hits; double estimate; std::size_t blocks_used; };

McPiResult mc_pi_serial  (std::size_t n, std::size_t block_size = 1);
McPiResult mc_pi_parallel(ThreadPool& pool, std::size_t n, std::size_t block_count);
```

For identical sample values in serial and parallel, pass the SAME block
size: serial `block_size = ceil(n / blocks)` with parallel `block_count =
blocks`. `hits` is exact (integers) and partition-order independent.

Matrix product:

```cpp
struct Matrix { std::size_t rows, cols; std::vector<double> data; /* (r,c) */ };

void   validate_matmul(const Matrix& a, const Matrix& b);   // throws std::invalid_argument
Matrix random_matrix(std::size_t rows, std::size_t cols, std::uint64_t seed);

void matmul_rows(const Matrix& a, const Matrix& b, Matrix& c,
                 std::size_t r0, std::size_t r1);           // shared kernel

struct MatmulResult { Matrix c; double checksum; std::size_t blocks_used; };

MatmulResult matmul_serial  (const Matrix& a, const Matrix& b);
MatmulResult matmul_parallel(ThreadPool& pool, const Matrix& a, const Matrix& b,
                             std::size_t block_count);
```

`c` is bit-identical between the two. `checksum` is tolerance-level only:
serial sums row-major in one pass, parallel merges block partials
left-to-right (see `docs/model.md` section 3). All functions throw
`std::invalid_argument` on empty inputs, mismatched dimensions or zero
block counts.

## Timing - `src/timing.hpp`

```cpp
class Stopwatch { void start(); double stop_sec(); };   // steady_clock

double median(std::vector<double> xs);   // 0 for empty input
double minimum_of(const std::vector<double>& xs);
```

## Bench driver - `pnb_bench`

```
pnb_bench --mode scaling|progress|equivalence|smoke
          [--out DIR]          # default docs/results
          [--repeats N]        # kept repeats per cell, default 8 (min 7)
          [--threads 1,2,4,8]  # default 1,2,4,8 + hardware_concurrency
          [--keep-cold]        # keep the first (cold) run in statistics
```

Output files (CSV schemas):

* `bench.csv` - `workload,variant,size,threads,run,kept,time_s,result`
  (variant `serial|parallel`; `kept=0` marks the discarded cold run;
  `result` is the MC estimate or the matrix checksum at 17 significant
  digits - identical strings across repeats of a cell prove in-run
  determinism).
* `progress.csv` - `checkpoint,samples_cum,hits_cum,estimate,abs_err`.
* `equivalence.csv` - `workload,size,threads,output_bit_identical,
  checksum_rel_delta,parallel_rerun_bit_stable`.
* `environment.txt` - compiler, build type, hardware concurrency, clock.

## Plotting scripts

```
python scripts/plot_results.py [--results docs/results] [--cpu "CPU string"]
python scripts/make_gif.py     [--results docs/results] [--out name.gif]
```

`plot_results.py` writes `scaling.png`, `equivalence_deltas.png`,
`speedup_table.md`, `run_summary.json` from the CSVs. `make_gif.py` writes
one frame per recorded `progress.csv` checkpoint (30-80 frames enforced).
Both use pure-white interiors and no grid lines.
