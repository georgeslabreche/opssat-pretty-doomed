# SEPP free memory

Output of `python3 mem_headroom.py`. Units: MiB. Idle is MemAvailable at the start of a run. Run peak is the memory that the v8 pipeline used. Margin is idle minus the peak (VmHWM) of a distress config on the SEPP emulator with a 1 GB limit.

| Machine | Run | Date | Total | Idle | Min | Run peak |
|---|---|---|---|---|---|---|
| EM | changelog/data/em-v2/run-00001 | 2026-03-24 | 1003 | 939 | 785 | 154 |
| EM | changelog/data/em-v2/run-00002 | 2026-03-24 | 1003 | 939 | 757 | 182 |
| EM | changelog/data/em-v2/run-00003 | 2026-03-24 | 1003 | 939 | 750 | 189 |
| EM | changelog/data/em-v3/pack-4023_1774447112/run-00001 | 2026-03-25 | 1003 | 939 | 769 | 170 |
| EM | changelog/data/em-v3/pack-4023_1774447112/run-00002 | 2026-03-25 | 1003 | 939 | 756 | 184 |
| EM | changelog/data/em-v3/pack-4023_1774447112/run-00003 | 2026-03-25 | 1003 | 939 | 758 | 181 |
| EM | changelog/data/em-v4/pack-4023_1774937270/run-00001 | 2026-03-31 | 1003 | 966 | 784 | 181 |
| EM | changelog/data/em-v4/pack-4023_1774937270/run-00002 | 2026-03-31 | 1003 | 965 | 783 | 183 |
| EM | changelog/data/em-v4/pack-4023_1774937270/run-00003 | 2026-03-31 | 1003 | 965 | 775 | 190 |
| EM | changelog/data/em-v5/pack-4023_1775155417/run-00001 | 2026-04-02 | 1003 | 968 | 741 | 227 |
| EM | changelog/data/em-v5/pack-4023_1775155417/run-00002 | 2026-04-02 | 1003 | 967 | 723 | 245 |
| EM | changelog/data/em-v6/pack-4023_1775579267/run-00001 | 2026-04-07 | 1003 | 966 | 768 | 197 |
| EM | changelog/data/em-v6/pack-4023_1775579267/run-00002 | 2026-04-07 | 1003 | 965 | 736 | 229 |
| EM | changelog/data/em-v6/pack-4023_1775661470/run-00001 | 2026-04-08 | 1003 | 965 | 766 | 199 |
| EM | changelog/data/em-v7/pack-4023_1784280414/run-00001 | 2026-07-17 | 1003 | 967 | 813 | 154 |
| EM | changelog/data/em-v7/pack-4023_1784317832/run-00001 | 2026-07-17 | 1003 | 964 | 765 | 199 |
| EM | changelog/data/em-v8/pack-flatsat-4023_1786988477/run-00001 | 2026-08-17 | 1003 | 970 | 795 | 175 |
| EM | changelog/data/em-v8/pack-flatsat-4023_1787033754/run-00001 | 2026-08-18 | 1003 | 970 | 768 | 202 |
| flight | flight/data/run-01-2026-04-21 | 2026-04-21 | 1003 | 964 | 756 | 208 |
| flight | flight/data/run-02-2026-05-22 | 2026-05-22 | 1003 | 964 | 762 | 201 |
| flight | flight/data/run-03-2026-06-09 | 2026-06-09 | 1003 | 968 | 767 | 201 |
| flight | flight/data/run-04-2026-06-10 | 2026-06-10 | 1003 | 968 | 767 | 201 |
| flight | flight/data/run-06-2026-07-27 | 2026-07-27 | 1003 | 968 | 724 | 244 |
| flight | flight/data/run-07-2026-07-27 | 2026-07-27 | 1003 | 968 | 733 | 234 |
| flight | flight/data/run-08-2026-08-26 | 2026-08-26 | 1003 | 968 | 768 | 200 |

| Machine | Runs | Idle, lowest | Idle, median | Idle, highest |
|---|---|---|---|---|
| EM | 18 | 939 | 965 | 970 |
| flight | 7 | 964 | 968 | 968 |

| Config | Peak | Margin, lowest idle | Margin, median idle |
|---|---|---|---|
| parakeet-0.6b-v2-int8 (10 s), EM | 869 | 70 | 96 |
| parakeet-0.6b-v2-int8 (10 s), flight | 869 | 94 | 98 |
| parakeet-0.6b-v2-int8-w5, EM | 821 | 118 | 144 |
| parakeet-0.6b-v2-int8-w5, flight | 821 | 142 | 146 |
