# Polyanya

Polyanya is a compromise-free pathfinding algorithm over navigation meshes (Read the paper [here](http://www.ijcai.org/proceedings/2017/0070.pdf).)
The implementation of Polyanya can be found [here](https://bitbucket.org/mlcui1/polyanya).

# Replicating our experiments
0. `make fast` compile and get executable
1. `./gen_pts.sh` generate target set;
2. `./gen_test.sh` generate input files;
3. `./gen_run.sh` generate scripts for experiments (dense and sparse);
4. `./s1run.sh` (generated by step 3) run dense experiment;
5. `./s2run.sh` (generated by step 3) run sparse experiment;
6. logs are stored in `output/{s1,s2}/*.log`.

# License

This implementation is licensed under GPLv2. Please refer to
`LICENSE` for more information.

Note that several source files from Daniel Harabor's
[Warthog project](https://bitbucket.org/dharabor/pathfinding)
(also licensed under GPLv2) were used in this project with their permission.
These files are:
`helpers/cfg.cpp`, `helpers/cfg.h`, `helpers/cpool.h`, `helpers/timer.cpp` and
`helpers/timer.h`.

Additionally, Fade2D is used to generate triangulations for use with this
with this implementation. Please note that commercial use of Fade2D requires
a valid commercial license.
