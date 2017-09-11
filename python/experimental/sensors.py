#------------------------------------------------------------------------------
# Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
# Copyright : IBM Research Ireland, 2017
#------------------------------------------------------------------------------

#import pdb; pdb.set_trace()           # enables debugging

import matplotlib                       # ; matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import traceback
import math
import numpy as np
import scipy.misc
import random

# Domain sizes:
Nx = int(127)
Ny = int(97)
# Fraction of points occupied by sensors:
FRACTION = 0.01
# Weight on mutual expulsion of sensors points:
WEIGHT = 2.0
# Number of sensors:
Ns = int(min(max(int(math.ceil(FRACTION * Nx * Ny)), int(1)), int(Nx * Ny)))


def InitialSensorDistribution():
    """ Function generates initial space distribution of sensor points.
    """
    x = np.zeros((Ns,))
    y = np.zeros((Ns,))
    for k in range(Ns):
        x[k] = random.uniform(0.0, 1.0)
        y[k] = random.uniform(0.0, 1.0)
    return x, y


def Evaluate(x, y):
    """ Function evaluates objective function and its gradient.
    """
    assert x.size == y.size
    N = x.size
    EPS = math.sqrt(np.finfo(float).eps)
    W_div_N = 1 # WEIGHT / N

    J = 0.0
    gradJ = np.zeros((2,N))

    for i in range(N):
        # Reciprocal distances to subdomain borders.
        r_x1 = 1.0 / (       x[i] **2 + EPS)
        r_x2 = 1.0 / ((1.0 - x[i])**2 + EPS)
        r_y1 = 1.0 / (       y[i] **2 + EPS)
        r_y2 = 1.0 / ((1.0 - y[i])**2 + EPS)

        J += (r_x1 + r_x2 +
              r_y1 + r_y2)

        gx = 0.0
        gy = 0.0
        for j in range(N):
            dx = x[i] - x[j]
            dy = y[i] - y[j]
            sqdist = float(dx*dx + dy*dy + EPS)
            J  += W_div_N / sqdist
            gx -= dx / sqdist**2
            gy -= dy / sqdist**2

        gradJ[0,i] = 2.0 * (W_div_N * gx - x[i] * r_x1**2 + (1.0 - x[i]) * r_x2**2)
        gradJ[1,i] = 2.0 * (W_div_N * gy - y[i] * r_y1**2 + (1.0 - y[i]) * r_y2**2)

    return J, gradJ


def DistributeSensors():
    """ Function evenly distributes sensors in the domain.
        It uses gradient descent to minimize objective function, which measures
        the mutual sensor points' expulsion and their expulsion from the borders.
        Experiments showed that quadratic distances (used here) work better
        than absolute values of the distances.
    """
    EPS = math.sqrt(np.finfo(float).eps)
    x, y = InitialSensorDistribution()
    ini_x = np.copy(x)
    ini_y = np.copy(y)
    tilde_x = np.copy(x)
    tilde_y = np.copy(y)
    J, gradJ = Evaluate(x, y)
    step = 0.1
    tol = 0.1
    inertia = 1.0 - 3.0/5.0

    print('Initial J = ' + str(J))
    proceed = True
    iter_no = 1
    while proceed:
        print('.', end='', flush=True)
        tilde_x_new = x - step * gradJ[0,:]
        tilde_y_new = y - step * gradJ[1,:]
        if iter_no > 1:
            x_new = inertia * tilde_x_new + (1.0 - inertia) * tilde_x
            y_new = inertia * tilde_y_new + (1.0 - inertia) * tilde_y
        else:
            x_new = tilde_x_new
            y_new = tilde_y_new

        if any(x_new < 0.0) or any(x_new > 1.0) or any(y_new < 0.0) or any(y_new > 1.0):
            print('1', end='', flush=True)
            step *= tol
            continue
        J_new, gradJ_new = Evaluate(x_new, y_new)
        if J < J_new:
            #print('2', end='', flush=True)
            print(str(J) + '   ' + str(J_new))
            step *= tol
            continue
        proceed = (J - J_new > np.finfo(float).eps * J)
        np.copyto(tilde_x, tilde_x_new)
        np.copyto(tilde_y, tilde_y_new)
        np.copyto(x, x_new)
        np.copyto(y, y_new)
        J = J_new
        np.copyto(gradJ, gradJ_new)
        step *= 2.0
        inertia = 1.0 - 3.0 / (5.0 + iter_no)
        print(inertia)
        iter_no += 1
    print('\n  Final J = ' + str(J))
    print('\n\n')

    plt.figure(1)
    plt.subplot(121, autoscale_on=False, aspect=float(Nx)/float(Ny))
    plt.plot(ini_x * (Nx-1), ini_y * (Ny-1), 'o')
    plt.axis([0, Nx, 0, Ny])
    plt.title('Before')
    plt.subplot(122, autoscale_on=False, aspect=float(Nx)/float(Ny))
    plt.plot(x * (Nx-1), y * (Ny-1), 'o')
    plt.axis([0, Nx, 0, Ny])
    plt.title('After')
    plt.tight_layout()
    plt.show()

if __name__ == '__main__':
    try:
        random.seed()
        DistributeSensors()
        #input('Press the "ENTER" key to quit ... ')
    except Exception as error:
        traceback.print_exc()
        print('ERROR: ' + str(error.args))

