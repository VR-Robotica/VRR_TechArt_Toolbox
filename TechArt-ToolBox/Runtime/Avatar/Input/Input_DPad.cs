using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.Input
{
    public enum DpadDirection
    {
        Up,
        Down,
        Left,
        Right,
    }

    public class Input_DPad
    {
        public float yMax;
        public float yMin;
        public float xMax;
        public float xMin;

        bool InRange(Vector2 position)
        {
            return InYRange(position.y) && InXRange(position.x);
        }

        bool InYRange(float value)
        {
            return value < yMax && value > yMin;
        }

        bool InXRange(float value)
        {
            return value < xMax && value > xMin;
        }

        public static bool GetDpad(DpadDirection direction, Vector2 joystickPosition)
        {
            return dpadToJoystickRange[direction].InRange(joystickPosition);
        }

        static Dictionary<DpadDirection, Input_DPad> dpadToJoystickRange = new Dictionary<DpadDirection, Input_DPad>
        {
            { DpadDirection.Up,     new Input_DPad { yMax = 10,     yMin = 0.3f,    xMax = 0.8f,    xMin = -0.8f    } },
            { DpadDirection.Down,   new Input_DPad { yMax = -0.3f,  yMin = -10,     xMax = 0.8f,    xMin = -0.8f    } },
            { DpadDirection.Left,   new Input_DPad { yMax = 0.8f,   yMin = -0.8f,   xMax = -0.3f,   xMin = -10f     } },
            { DpadDirection.Right,  new Input_DPad { yMax = 0.8f,   yMin = -0.8f,   xMax = 10f,     xMin = 0.3f     } }
        };
    }
}