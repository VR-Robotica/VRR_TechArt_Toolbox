using System.Collections.Generic;
using UnityEngine;
using VRR.Input;

#if OVR_PLUGIN
namespace VRR.Avatar.Hands
{
    /// <summary>
    /// Oculus Touch related Input combinations that translate into hand poses
    /// </summary>

    public class HandPose_Input_OculusController : Input_OculusController, IHandControllerInput
    { 
        private const float MAX_THRESHOLD = 0.9f;
        private const float MIN_THRESHOLD = 0.1f;

        #region INTERFACE POSE CALLBACKS
        /// <summary>
        /// Confirm if the hand is currently posing...
        /// </summary>
        public bool IsPosing(ControllerHand hand)
        {
            return (IsPointing(hand) || IsPointing_ThumbUp(hand) || IsThumbsUp(hand) || IsOkaySign(hand));
        }

        /// <summary>
        /// If the grip trigger is being pulled, then we are grabbing
        /// this is a general reference used with a collider to indicate a grabbing action override
        /// </summary>
        public bool IsGrabbing(ControllerHand hand)
        {
            return GetGripAmount(hand) > MAX_THRESHOLD;
        }
        
        /// <summary>
        /// If input values of index trigger and grip trigger are below the minimal threshold, then the hand must be resting
        /// </summary>
        public bool IsResting(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return GetGripAmount(hand)    < MIN_THRESHOLD &&
                   GetIndexAmount(hand) < MIN_THRESHOLD &&
                   !IsThumbTouching(hand);
        }

        /// <summary>
        /// If index and thumb are touching and grip trigger is pulled, then we are making a fist!
        /// </summary>
        public bool IsFist(ControllerHand hand)
        {
            return IsIndexNearTouching(hand) && GetGripAmount(hand) > MAX_THRESHOLD && IsThumbTouching(hand);
        }

        /// <summary>
        /// If grip trigger is pulled, thumb is touching, but index is NOT pulled nor touching, then we are pointing!
        /// </summary>
        public bool IsPointing(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return !IsIndexNearTouching(hand) && GetGripAmount(hand) > MAX_THRESHOLD && IsThumbTouching(hand);
        }

        /// <summary>
        /// If grip trigger is pulled, but thumb and index are NOT touching, then we are pointing with the thumb up!
        /// </summary>
        public bool IsPointing_ThumbUp(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return !IsIndexNearTouching(hand) && GetGripAmount(hand) > MAX_THRESHOLD && !IsThumbTouching(hand);
        }

        /// <summary>
        /// If the grip and index triggers are pulled but the thumb is not touching anything, then we give a thumbs up
        /// </summary>
        public bool IsThumbsUp(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return GetIndexAmount(hand) > MAX_THRESHOLD && GetGripAmount(hand) > MAX_THRESHOLD && !IsThumbTouching(hand);
        }

        /// <summary>
        /// If thumb is touching, index is pulled and touching, but grip trigger is NOT pulled, then we are making okay sigh (or pinching)
        /// </summary>
        public bool IsOkaySign(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return GetIndexAmount(hand) > MAX_THRESHOLD && GetGripAmount(hand) < 0.05f && IsThumbTouching(hand);
        }
        #endregion
    }
}
#endif