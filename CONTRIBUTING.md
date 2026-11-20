# Branching and contribution workflow

## Branches

| branch | purpose |
| --- | --- |
| `main` | **stable releases only**. Default branch. Never commit work-in-progress here. |
| `master` | legacy alias of `main`, kept for tooling that expects that name. |
| `develop` | integration branch. All day-to-day work happens here. |
| `features` | base branch for topic work. Create `features/<topic>` from `develop`. |
| `haseeb-heaven` | original branch kept from the initial push. |

## Rules

1. Do all development on `develop` or a `features/<topic>` branch.
2. Open pull requests from `features/<topic>` into `develop`.
3. Only promote `develop` to `main` (and `master`) for a tested, tagged
   stable release.
4. Never push directly to `main`/`master` with unreviewed changes.
5. Keep commits small and focused: one feature, fix, test group or doc change
   per commit.
6. Every behaviour change must include executable verification:
   `./tests/run_tests.sh`, `./demos/run_demos.sh` and, where relevant,
   `./benchmarks/run_bench.sh`.

## Local setup

```sh
git clone https://github.com/haseeb-heaven/c-plus.git
cd c-plus
git checkout develop
```

## Typical change

```sh
git checkout develop
git pull
git checkout -b features/my-change
# edit, build, test
git add <specific files>
git commit -m "feat: describe the change"
git push -u origin features/my-change
# open PR: features/my-change -> develop
```

After the PR is green and merged, `develop` is the integration state. A
release is a separate PR from `develop` into `main`.

## Verification before any release PR

```sh
cmake -S . -B build
cmake --build build --target c+ -- -j4
ctest --test-dir build --output-on-failure
./tests/run_tests.sh
./demos/run_demos.sh
./benchmarks/run_bench.sh
```
