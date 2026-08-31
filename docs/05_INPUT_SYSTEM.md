# 05 — Input System & Hardware Digitizer Abstraction Layer (HDAL)

## Overview

Interactive Smart Boards and Interactive Flat Panel Displays (IFPDs) employ vastly different touch hardware technologies. Unlike consumer tablets (which use capacitive or EMR active pens), commercial smartboards rely primarily on **Infrared (IR) Laser Grid Frames** (X/Y optical emitters and sensors around the frame), **Optical Camera Tracking**, **Electromagnetic (EMR)**, or **Projected Capacitive (P-CAP)** digitizers.

OpenBoard includes a dedicated **Hardware Digitizer Abstraction Layer (HDAL)** in C++ to guarantee ultra-fast, smooth, and jitter-free performance across **ANY hardware smartboard brand** (ViewSonic, SMART Board, Promethean, Huawei IdeaHub, MAXHUB, BenQ, newline, or generic Android TV IFPDs).

---

## 🛰️ Hardware Digitizer Abstraction Layer Architecture (HDAL)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                   Raw Hardware Digitizer Inputs                          │
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────────────┐ │
│  │ IR Laser Frame   │  │ Optical Camera   │  │ EMR / Active Stylus    │ │
│  │ (Optical X/Y Grid)│ │ (Corner Cameras) │  │ (Samsung S Pen / Wacom)│ │
│  └────────┬─────────┘  └────────┬─────────┘  └───────────┬────────────┘ │
└───────────┼─────────────────────┼────────────────────────┼──────────────┘
            │                     │                        │
            ▼                     ▼                        ▼
┌──────────────────────────────────────────────────────────────────────────┐
│             Hardware Digitizer Abstraction Layer (HDAL)                  │
│                                                                          │
│  - Hardware Touch Type Auto-Detection                                    │
│  - Kalman Filter Coordinate Jitter Reduction (for IR Laser Frames)       │
│  - Centroid Bounding Box Normalizer                                      │
│  - Adaptive Sample Rate Interpolation (60Hz -> 240Hz Virtual Curve)      │
│  - Optical Ghost Touch & Palm Rejection Filter                           │
└─────────────────────────────────┬────────────────────────────────────────┘
                                  │ Normalized InputEvent Stream
                                  ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                OpenBoard Engine (ToolManager & Skia)                     │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 🛠️ Supported Digitizer Technologies & Optimization Rules

| Digitizer Type | Hardware Mechanism | Specific Noise / Latency Challenge | OpenBoard HDAL Compensation Strategy |
|────────────────|────────────────────|────────────────────────────────────|──────────────────────────────────────|
| **IR Laser Grid Frame** *(Most Common)* | Infrared emitters & phototransistors along bezel frames create an X/Y optical grid. | - Touch point wiggling (jitter).<br>- Large contact radius (`TOUCH_MAJOR`).<br>- Multi-point optical refraction. | **Kalman Centroid Filter**: Smoothes raw X/Y coordinate noise. Calculates geometric centroid of contact area for razor-sharp stroke placement. |
| **Optical Camera System** | Cameras in top corners track physical pen/finger shadow. | - Ambient light interference.<br>- Edge latency near corners. | **Adaptive Velocity Predictor**: Extrapolates missing touch sample packets during fast hand swipes. |
| **EMR / Active Stylus** | Electromagnetic sensor board behind display. | - Requires specialized vendor pen API. | **Native Pressure Curve Mapping**: Directly maps physical pressure levels (4096 levels) and tilt angles (\(\pm 60^\circ\)). |
| **Capacitive (P-CAP)** | Transparent ITO conductive grid on glass. | - Sub-millimeter accuracy, low noise. | **Pass-through Low Latency**: Direct path execution bypassing heavy filtering. |

---

## 🧮 IR Laser Frame Jitter Reduction (Kalman Centroid Filtering)

Infrared laser grid frames report touch coordinates as coarse bounding boxes that fluctuate slightly even when holding a physical pen still against the board. HDAL applies a dual-state **Kalman Filter**:

```cpp
class KalmanTouchFilter {
private:
    float x_est = 0.0f, y_est = 0.0f;
    float P_x = 1.0f, P_y = 1.0f;
    float Q = 0.022f; // Process noise covariance
    float R = 0.650f; // Measurement noise covariance (tuned for IR frames)

public:
    SkPoint filter(float rawX, float rawY) {
        // Time update (Predict)
        float P_x_temp = P_x + Q;
        float P_y_temp = P_y + Q;

        // Measurement update (Correct)
        float K_x = P_x_temp / (P_x_temp + R);
        float K_y = P_y_temp / (P_y_temp + R);

        x_est = x_est + K_x * (rawX - x_est);
        y_est = y_est + K_y * (rawY - y_est);

        P_x = (1.0f - K_x) * P_x_temp;
        P_y = (1.0f - K_y) * P_y_temp;

        return SkPoint::Make(x_est, y_est);
    }
};
```

---

## 🎯 4-Point & 9-Point Hardware Board Calibration

For older interactive projectors or misaligned IR laser frames, OpenBoard includes a built-in **Hardware Touch Calibration Tool**.

- Renders target bullseye targets in corners and center.
- Computes a 3x3 affine transformation matrix \(M_{calib}\) to map distorted physical digitizer coordinates back to screen pixels:

$$\begin{bmatrix} X_{screen} \\ Y_{screen} \\ 1 \end{bmatrix} = \mathbf{M}_{calib} \begin{bmatrix} X_{raw} \\ Y_{raw} \\ 1 \end{bmatrix}$$
