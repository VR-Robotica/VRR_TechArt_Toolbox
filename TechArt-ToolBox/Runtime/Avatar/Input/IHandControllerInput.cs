using UnityEngine;
using VRR.Input;

namespace VRR.Avatar.Hands
{
    public interface IHandControllerInput : IControllerInput, IHandPoses
    {
        // used to combine interfaces of Hand Input and Hand Pose Callbacks
    }
}