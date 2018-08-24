# -----------------------------------------------------------------------------
# Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
# Copyright : IBM Research Ireland, 2017-2018
# -----------------------------------------------------------------------------

""" This script runs several simulations with increasing problem size
    utilizing all available CPUs. It saves the execution time of each
    simulation in a file that can be used to plot the scalability profile.
    Essential parameters, listed in the first lines, include the set of
    problem sizes and integration period.
      Each simulation is twofold. First, we run the Python forward solver that
    generates the ground-truth and observations ("ObservationsGenerator.py").
    The Python code itself uses the C++ code running in the special mode for
    generating sensor locations (scenario "sensors"). Second, the C++ data
    assimilation application is launched (scenario "simulation") with
    observations generated by "ObservationsGenerator.py".
      The results of all the simulations are accumulated in the output
    directory and can be visualized later on by the script "Visualize.py".
      The configuration file "amdados.conf" is used in all the simulations with
    modification of three parameters: grid sizes (number of subdomains) in both
    dimensions and integration time. Other parameters remain intact. It is not
    recommended to tweak parameters unless their meaning is absolutely clear.
      If you had modified the parameters, please, consider to rerun this script
    because the results in the output directory a not valid any longer.
      The script was designed to fulfil the formal requirements of the
    Allscale project.
"""
print(__doc__)

#import pdb; pdb.set_trace()           # enables debugging
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import sys, traceback, os, glob, getopt, math, argparse, subprocess
import numpy as np
import numpy.linalg
import scipy
import scipy.misc
import scipy.sparse
import scipy.sparse.linalg
from timeit import default_timer as timer
from Configuration import Configuration
from ObservationsGenerator import InitDependentParams, Amdados2D
from Utility import *

# Grid sizes (number of subdomains in each direction) for scalability tests.
# This is a set of big problems, can take forever: time -> infinity ...
#GridSizes = [(11,11), (23,25), (43,41), (83,89), (167,163), (367,373)]
# This is a reasonable set of problems, be patient for days to come ...
#GridSizes = [(11,11), (19,17), (23,25), (37,31), (43,41), (83,89)]
# Small problems for a relatively brief testing.
#GridSizes = [(13,11), (17,13), (19,15), (23,21), (25,23), (27,25)]
#GridSizes = [(13,11), (18,16), (23,21), (29,27), (34,32), (39,37)]
GridSizes = [(2,2),(4,4),(8,8),(12,12),(16,16),(20,20),(24,24),(28,28),(32,32)]

# Integration period in seconds.
IntegrationPeriod = 100 

# Path to the C++ executable.
AMDADOS_EXE = "build/app/amdados"


if __name__ == "__main__":
    try:
        CheckPythonVersion()
        # Read configuration file.
        conf = Configuration("amdados.conf")
        # Create the output directory, if it does not exist.
        if not os.path.isdir(conf.output_dir):
            os.mkdir(conf.output_dir)
        # Check existence of "amdados" application executable.
        assert os.path.isfile(AMDADOS_EXE), "amdados executable was not found"

        # For all the grid sizes in the list ...
        exe_time_profile = np.zeros((len(GridSizes),2))
        for grid_no, grid in enumerate(GridSizes):
            assert grid[0] >= 2 and grid[1] >= 2, "the minimum grid size is 2x2"
            # Modify parameters given the current grid size.
            setattr(conf, "num_subdomains_x", int(grid[0]))
            setattr(conf, "num_subdomains_y", int(grid[1]))
            setattr(conf, "integration_period", int(IntegrationPeriod))
            InitDependentParams(conf)
            conf.PrintParameters()
            config_file = conf.WriteParameterFile("scalability_test.conf")
            subprocess.run("sync", check=True)

            # Python simulator generates the ground-truth and observations.
            Amdados2D(config_file, False)

            # Get the starting time.
            start_time = timer()

            # Run C++ data assimilation application.
            print("##################################################")
            print("Simulation by 'amdados' application ...")
            print("silent if debugging & messaging were disabled")
            print("##################################################")
            subprocess.run([AMDADOS_EXE, "--scenario", "simulation",
                                "--config", config_file], check=True)

            # Get the execution time and corresponding (global) problem size
            # and save the current scalability profile into the file.
            problem_size = ( conf.num_subdomains_x * conf.subdomain_x *
                             conf.num_subdomains_y * conf.subdomain_y )
            exe_time_profile[grid_no,0] = problem_size
            exe_time_profile[grid_no,1] = timer() - start_time
            np.savetxt(os.path.join(conf.output_dir, "scalability_size.txt"),
                       exe_time_profile)

    except subprocess.CalledProcessError as error:
        traceback.print_exc()
        if error.output is not None:
            print("ERROR: " + str(error.output))
        else:
            print("CalledProcessError")
    except AssertionError as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))
    except ValueError as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))
    except Exception as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))

