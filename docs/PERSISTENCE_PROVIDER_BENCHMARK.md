# SQLite and RocksDB preliminary comparison

Both providers meet the same native contract and preserve logical state in both migration directions. SQLite returned records faster on the measured read paths and used less peak memory. RocksDB opened snapshots faster, used less disk after maintenance and kept a much smaller live WAL while a snapshot was pinned. The observed trade-offs apply to this workload and machine.

## Method

Three paired trials used Windows 10 Pro 19045, MSVC Visual Studio 2022 x64 Debug, a four-core Intel Core i5-6500 at 3.20 GHz, 16 GiB of RAM and a Toshiba HDWE140 SATA disk. Each trial used fresh store directories. Trial order alternated, with SQLite first in trials 1 and 3 and RocksDB first in trial 2. Benchmarks ran without an Engine build in progress.

The benchmark seeded 2,048 records in groups of 32. Each record had two signed structured fields, a composite index and 192 deterministic binary bytes. Each trial ran 2,048 point reads, 128 bulk reads, 512 bounded range queries, 256 transactions of eight updates and four simultaneous readers performing 512 point reads each. Retention was 64 revisions. A snapshot remained open during the transaction phase. Each backend used a 4 MiB cache setting. SQLite's cache is per connection, and RocksDB's is a shared block cache. RocksDB used a 256 KiB write buffer to exercise flush and compaction in the small test. The comparison used SQLite WAL FULL and RocksDB synchronous WAL, with RocksDB compression disabled.

Each table cell is the median of the three run-level measurements. Latency percentiles are nearest rank. Rates count a full query, bulk request or transaction as one operation. Concurrent throughput includes opening the four reader snapshots. Peak RSS covers the whole benchmark process. Disk measurements sum observed file lengths, and recovery timing ends after the first validated read in a fresh process. They cover process termination and recovery on this host.

## Latency and throughput

| Operation | Calls per trial | SQLite median / p95 / p99 µs | RocksDB median / p95 / p99 µs | SQLite ops/s | RocksDB ops/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Open and release snapshot | 64 | 802.7 / 1,446.2 / 2,158.6 | 85.4 / 161.3 / 406.9 | 1,060.3 | 9,463.7 |
| Point read | 2048 | 61.8 / 130.6 / 236.1 | 120.4 / 231.2 / 421.2 | 13,303.6 | 7,250.8 |
| Bulk read of 32 keys | 128 | 2,262.1 / 3,486.6 / 4,870.5 | 2,451.9 / 4,091.3 / 4,927.8 | 404.7 | 373.9 |
| Composite range of 16 rows | 512 | 990.7 / 1,657.5 / 1,997.5 | 2,759.8 / 3,731.6 / 5,865.1 | 909.3 | 332.4 |
| Commit 8 updates | 256 | 40,861.2 / 50,209.3 / 74,768.5 | 33,081.2 / 54,235.5 / 75,116.5 | 25.5 | 28.9 |
| Point read across four readers | 2048 | 92.7 / 207.2 / 837.7 | 360.3 / 548.6 / 1,217.3 | 26,788.3 | 10,924.4 |

Commit throughput ranged from 21.5–25.8 transactions/s for SQLite and 28.3–29.2 for RocksDB. RocksDB's median throughput was higher in this run. The measured p99 commit latencies were close.

## Storage memory and recovery

| Measurement | SQLite | RocksDB |
| --- | ---: | ---: |
| Whole-process peak RSS, MiB | 20.863 | 35.727 |
| WAL files before pinned snapshot, MiB | 0.000 | 0.000 |
| WAL files with pinned snapshot, MiB | 20.314 | 0.208 |
| Store files with pinned snapshot, MiB | 22.134 | 2.368 |
| WAL files after maintenance, MiB | 0.000 | 0.000 |
| Store files after maintenance, MiB | 1.824 | 0.813 |
| Retained journal revisions | 64 | 64 |
| Checkpoint or flush and compaction, ms | 131.4 | 309.3 |
| Clean reopen to validated read, ms | 15.0 | 187.5 |
| Crash recovery to validated read, ms | 146.6 | 265.8 |
| Crash recovery trial range, ms | 131.9–170.4 | 257.3–317.2 |

With the SQLite read snapshot pinned at the pre-update revision, a passive checkpoint saw 5,170 WAL frames and checkpointed 0. Releasing the snapshot let the truncate checkpoint reduce the WAL to zero bytes. RocksDB's explicit flush and compaction reclaimed obsolete files after the pinned snapshot was released.

For RocksDB, the three-run medians were 3,083,400 write-batch bytes, 3,083,400 WAL bytes, 2,675,306 flush bytes and 5,744,841 compaction bytes. The median internal write-amplification proxy was **3.73×**, calculated per trial as (WAL bytes + flush bytes + compaction bytes) divided by write-batch bytes. This proxy includes the provider's record, index and journal writes. It measures RocksDB's reported activity and excludes filesystem overhead. RocksDB's block cache held 2.113 MiB at the measurement point.

## Reproducing the trial

Build `CreationEnginePersistenceBenchmark` in the canonical Engine build directory. From the Engine repository, run the following in PowerShell. Use a new directory for each trial and alternate provider order.

```powershell
$bench = './build/Debug/CreationEnginePersistenceBenchmark.exe'
$trial = Join-Path (Resolve-Path ./build).Path ('persistence-trial-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
foreach ($provider in @('sqlite', 'rocksdb')) {
    & $bench run $provider (Join-Path $trial $provider) 2048
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark run failed' }
    & $bench crash-seed $provider (Join-Path $trial "$provider-crash") 2048
    if ($LASTEXITCODE -ne 71) { throw 'Expected abrupt process exit' }
    & $bench recover $provider (Join-Path $trial "$provider-crash") 2048
    if ($LASTEXITCODE -ne 0) { throw 'Recovery probe failed' }
}
```

## Provider recommendations

Evaluate SQLite first for local deployments that prioritize reads, predictable memory use and straightforward recovery. Compare RocksDB for workloads that open many short-lived snapshots, retain readers during writes, or require a smaller post-maintenance footprint. Both remain supported. Before selecting a production default, repeat the test with Release binaries, SSD storage, larger datasets, and different snapshot and compaction settings.

## Considerations for ENG-004

Map `HistoryUnavailable` and `ResourceLimit` to explicit API results. Issue transport cursors scoped to caller, session, revision and expiry. Define deployment-wide byte and snapshot budgets before exposing logical image export, and use a streamed transfer for large sessions. Keep provider tuning and native maintenance under operator controls.
