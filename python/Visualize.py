# -----------------------------------------------------------------------------
# Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
# Copyright : IBM Research Ireland, 2017-2018
# -----------------------------------------------------------------------------

""" Script visualizes simulation results given a path to field*.txt file.
    Note, this script visualizes results of one particular simulation.
    The reason for that is to have flexibility for debugging.
    One can also visualize all the simulation results in bash
    terminal (mind the mandatory parameter "--field"):

    for f in output/field_*.bin \
    do \
        python3 python/Visualize.py --field $f \
    done

    or in a single line:

    for f in output/field_*.bin; do python3 python/Visualize.py --field $f; done

"""
print(__doc__)

#import pdb; pdb.set_trace()           # enables debugging

import numpy as np
from scipy.stats import mstats
import scipy.misc
import matplotlib
matplotlib.use("Agg")
from matplotlib.ticker import MultipleLocator
import matplotlib.animation as manimation
import sys, traceback, os, math, argparse
from Utility import *
from Configuration import Configuration


def GetTrueFieldFilename(filename):
    """ Given the name of the solution file, function constructs the name
        of the "true fields" solution file.
    """
    dirname = os.path.dirname(filename)
    basename = os.path.basename(filename)
    if not basename.startswith("field_"):
        sys.exit("\nPlease, use the file named 'field_***.txt' as an input.\n")
    return os.path.join(dirname, "true_" + basename)


def PlotRelativeDifference(field_filename, rel_diff):
    """ Function plots and save in the file the difference between data
        assimilation simulation and analytic one.
    """
    dirname = os.path.dirname(field_filename)
    Nx, Ny, _ = ProblemParametersFromFilename(field_filename, True, "field")
    filename = "rel_diff_Nx" + str(Nx) + "_Ny" + str(Ny) + ".png"

    matplotlib.rcParams["legend.fontsize"] = 10
    fig = plt.figure()
    ax = fig.gca()
    ax.plot(np.arange(1, len(rel_diff) + 1), rel_diff)
    ax.set_xlabel("relative time: 100 * t / T")
    ax.set_ylabel("relative difference")
    plt.title("|analytic - simulation| / |analytic|")
    plt.savefig(os.path.join(dirname, filename))


def PlotSensors(field_filename, conf):
    """ Function plots the sensor locations.
    """
    # Load file of sensors corresponding to the file of solution fields.
    dirname = os.path.dirname(field_filename)
    Nx, Ny, _ = ProblemParametersFromFilename(field_filename, True, "field")
    sensors_filename = os.path.join(dirname,
                            "sensors_Nx" + str(Nx) + "_Ny" + str(Ny) + ".txt")
    sensors = np.loadtxt(sensors_filename)
    assert Nx % conf.subdomain_x == 0 and Ny % conf.subdomain_y == 0
    # Do plotting with grid spacing at the subdomain boundaries.
    fig = plt.figure()
    ax = fig.gca()
    ax.scatter(sensors[:,0], sensors[:,1], s=max(min(Nx,Ny)*0.01, 1.0))
    ax.grid(which="minor", color="r", linestyle=":")
    ax.xaxis.set_minor_locator(MultipleLocator(conf.subdomain_x))
    ax.yaxis.set_minor_locator(MultipleLocator(conf.subdomain_y))
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_xlim((0, Nx))
    ax.set_ylim((0, Ny))
    plt.title("Sensor locations")
    plt.tight_layout()
    plt.savefig(os.path.join(dirname,
                        "sensors_Nx" + str(Nx) + "_Ny" + str(Ny) + ".png"))


def PlotSubiterProfile(field_filename, conf):
    """ Function plots a picture demonstrating how smoothness of the solution
        improves at the subdomain boundaries as sub-terations progressing.
        If the file of sub-iteration profile does not exist (this is only
        available with AMDADOS_DEBUGGING flag defined in C++ application),
        then nothing is done.
    """
    Nsubiter = conf.Nsubiter
    # Load file of sensors corresponding to the file of solution fields.
    dirname = os.path.dirname(field_filename)
    Nx, Ny, Nt = ProblemParametersFromFilename(field_filename, True, "field")
    profile_filename = os.path.join(dirname,
                    "subiter_diff_Nx" + str(Nx) + "_Ny" + str(Ny) + ".txt")
    if not os.path.isfile(profile_filename):
        print("WARNING: file " + profile_filename + " does not exist")
        return
    profile = np.loadtxt(profile_filename)
    if len(profile.shape) != 1 or len(profile) != Nt * Nsubiter:
        print("WARNING: empty or incomplete file of sub-iteration differences")
        return
    profile = np.reshape(profile, (Nt, Nsubiter))
    # Compute quantiles among Nt samples of size Nsubiter.
    quantiles = mstats.mquantiles(profile, axis=0)
    # Plotting the picture of the relative difference behaviour.
    fig = plt.figure()
    ax = fig.gca()
    labels = ["25%", "50%", "75%"]
    for i, q in enumerate(quantiles):
        plt.plot(np.arange(1, Nsubiter+1), q, label=labels[i])
    plt.legend()
    ax.set_xlabel("Sub-iteration")
    ax.set_ylabel("Relative difference")
    plt.title("Decay of relative difference at subdomain boundaries")
    plt.tight_layout()
    plt.grid()
    plt.savefig(os.path.join(dirname,
                    "subiter_diff_Nx" + str(Nx) + "_Ny" + str(Ny) + ".png"))


def MakeVideoFile(field_filename):
    """ Function makes the video file name from the field one.
    """
    dirname = os.path.dirname(field_filename)
    Nx, Ny, Nt = ProblemParametersFromFilename(field_filename, True, "field")
    video_filename = os.path.join(dirname,
            "video_Nx" + str(Nx) + "_Ny" + str(Ny) + "_Nt" + str(Nt) + ".avi")
    return video_filename


def MakePngFile(field_filename, title, t):
    """ Function makes the png-file name from the field one.
    """
    dirname = os.path.dirname(field_filename)
    png_filename = os.path.join(dirname, title + "-t=" + str(t) + ".png")
    return png_filename


if __name__ == "__main__":
    try:
        CheckPythonVersion()
        parser = argparse.ArgumentParser()
        parser.add_argument("--field", type=str, default=None,
                            help="path to solution field file.")
        opts = parser.parse_args()
        field_file = os.path.expanduser(opts.field)
        true_field_file = GetTrueFieldFilename(opts.field)
        print("Parameters:")
        print("Solution fields file: " + field_file)
        print("True solution fields file: " + true_field_file)
        print("")

        # Read the data files of the solution fields.
        print("Loading solution fields ...", flush=True)
        true_timestamps, true_fields = ReadResultFile(true_field_file)
        timestamps, fields = ReadResultFile(field_file)
        assert np.all(true_timestamps == timestamps), "mismatch in timestamps"
        assert np.all(true_fields.shape == fields.shape), "mismatch in layouts"
        assert len(timestamps) == fields.shape[0]
        print("done", flush=True)

        # Relative difference between assimilation and true solutions.
        rel_diff = np.zeros((fields.shape[0],))

        # Read configuration file.
        conf = Configuration("amdados.conf")

        # Create ffmpeg writer for video output.
        FFMpegWriter = manimation.writers["ffmpeg"]
        metadata = dict(title="Simulation with data assimilation",
                        artist="Matplotlib", comment="visualization")
        bitrate = None      # default
        bitrate = 4096      # high quality
        fps = 10            # slow, suitable for detailed inspection
        fps = 50            # fast
        video_writer = FFMpegWriter(fps=fps, bitrate=bitrate, metadata=metadata)

        # Plot the separate fields in video file.
        plt = SwitchToGraphicalBackend()
        fig = plt.figure()
        picture = None
        im = None
        with video_writer.saving(fig, MakeVideoFile(field_file), dpi=100):
            for i in range(fields.shape[0]):
                # Print information regarding the fields.
                if np.any(np.isinf(true_fields[i,:,:])):
                    print("WARNING: true field contains Inf value(s)")
                if np.any(np.isinf(fields[i,:,:])):
                    print("WARNING: field contains Inf value(s)")
                if np.any(np.isnan(true_fields[i,:,:])):
                    print("WARNING: true field contains NaN value(s)")
                if np.any(np.isnan(fields[i,:,:])):
                    print("WARNING: field contains NaN value(s)")
                if np.amin(true_fields[i,:,:]) < -3.0 * np.finfo(float).eps:
                    print("WARNING: true field contains negative value(s)")
                if np.amin(fields[i,:,:]) < -3.0 * np.finfo(float).eps:
                    print("WARNING: field contains negative value(s)")
                if False:
                    print("true field:", end="")
                    print("  min: " + str(np.amin(true_fields[i,:,:])) +
                          ", max: " + str(np.amax(true_fields[i,:,:])))
                    print("field:", end="")
                    print("  min: " + str(np.amin(fields[i,:,:])) +
                          ", max: " + str(np.amax(fields[i,:,:])))

                # Compute the relative difference between two solutions,
                # excluding the external boundary points.
                norm_true = np.linalg.norm(true_fields[i, 2:-2, 2:-2].ravel())
                norm_diff = np.linalg.norm(true_fields[i, 2:-2, 2:-2].ravel() -
                                                fields[i, 2:-2, 2:-2].ravel())
                rel_diff[i] = norm_diff / max(norm_true, np.finfo(float).eps)

                # Plot the solution fields.
                true_image = Field2Image(true_fields[i,:,:])
                image = Field2Image(fields[i,:,:])
                assert true_image.shape == image.shape
                t = str(timestamps[i])
                if False:
                    # If we want images for publication:
                    scipy.misc.imsave(MakePngFile(field_file, "true-field", t),
                                      true_image)
                    scipy.misc.imsave(MakePngFile(field_file, "field", t),
                                      image)
                gap = 20
                nr = image.shape[0]
                nc = image.shape[1]
                if picture is None: picture = np.ones((nr, 2*nc + gap))
                picture[0:nr, 0:nc] = true_image
                picture[0:nr, nc+gap:2*nc+gap] = image
                if i == 0:
                    im = plt.imshow(picture)
                else:
                    im.set_array(picture)
                plt.xticks([])
                plt.yticks([])
                plt.title("< true density vs estimated >, Nx=" +
                                str(nr) + ", Ny=" + str(nc) + ", t=" + t)
                if i == 0:
                    plt.get_current_fig_manager().full_screen_toggle()
                plt.draw()
                video_writer.grab_frame()
                plt.pause(0.05)

        PlotRelativeDifference(field_file, rel_diff)
        PlotSensors(field_file, conf)
        #PlotSubiterProfile(field_file, conf)

    except AssertionError as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))
    except ValueError as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))
    except Exception as error:
        traceback.print_exc()
        print("ERROR: " + str(error.args))



#https://matplotlib.org/examples/animation/moviewriter.html

