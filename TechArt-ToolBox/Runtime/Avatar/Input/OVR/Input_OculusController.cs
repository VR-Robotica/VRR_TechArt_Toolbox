using System.Collections;
using System.Collections.Generic;
using UnityEngine;

#if OVR_PLUGIN
namespace VRR.Input
{
    public enum OculusControllerType { Desktop, Android }

    public class Input_OculusController : OculusControllerMapping, IControllerInput
    {
        private OVRInput.Controller LeftController = OVRInput.Controller.LTouch;
        private OVRInput.Controller RightController = OVRInput.Controller.RTouch;

        #region GETTERS

        // analog values
        public float GetAxis1D(ControllerHand hand, OVRInput.Axis1D axis)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(axis, ovrController);
        }

        public Vector2 GetAxis2D(ControllerHand hand, OVRInput.Axis2D axis)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(axis, ovrController);
        }

        public Vector2 GetThumbstickAxis(ControllerHand hand)
        {
            return GetAxis2D(hand, Common_ThumbStick_Axis);
        }

        #endregion

        #region SETTER
        public void SetControllerType(OculusControllerType controllerType)
        {
            switch(controllerType)
            {
                case OculusControllerType.Desktop:
                    LeftController  = OVRInput.Controller.LTouch;
                    RightController = OVRInput.Controller.RTouch;
                    break;

                case OculusControllerType.Android:
                    LeftController = OVRInput.Controller.LTouch;
                    RightController = OVRInput.Controller.RTouch;
                    break;

                default:
                    LeftController = OVRInput.Controller.LTouch;
                    RightController = OVRInput.Controller.RTouch;
                    break;
            }
        }
        #endregion

        #region MISC RETURNS

        public bool IsJoystickInQuadrant(ControllerHand hand, DpadDirection quad)
        {
            var ovrController = ToOVRTouchHand(hand);
            var joystickPosition = OVRInput.Get(Common_ThumbStick_Axis, ovrController);
            return Input_DPad.GetDpad(quad, joystickPosition);
        }

        #endregion


        #region INTERFACE CALLBACKS
        public bool IsControllerConnected(ControllerHand hand)
        {
            switch (hand)
            {
                case ControllerHand.Left:
                    return OVRInput.IsControllerConnected(LeftController);

                case ControllerHand.Right:
                    return OVRInput.IsControllerConnected(RightController);

                default:
                    return false;
            }
        }

        public float GetHorizAxis(ControllerHand hand)
        {
            return GetAxis2D(hand, Common_ThumbStick_Axis).x;
        }

        public float GetVertAxis(ControllerHand hand)
        {
            return GetAxis2D(hand, Common_ThumbStick_Axis).y;
        }

        public bool  Recentering()
        {
            return OVRInput.Get(Left_ThumbStick_Press, LeftController) && OVRInput.Get(Right_ThumbStick_Press, RightController);
        }

        // index trigger values
        public bool  IsIndexNearTouching(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return OVRInput.Get(Common_Index_NearTouch, ovrController);
        }

        public bool IsIndexTouching(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return OVRInput.Get(Common_Index_Touch, ovrController);
        }

        public float GetIndexAmount(ControllerHand hand)
        {
            float amount = GetAxis1D(hand, Common_Index_Trigger);

            // analog trigger doesn't always return to zero when released
            if (amount < 0.01f)
                amount = 0.0f;

            return amount;
        }

        public bool  GetAlternateIndex(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(Common_Index_Button, ovrController);
        }

        public bool  GetAlternateIndexDown(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.GetDown(Common_Index_Button, ovrController);
        }

        public bool  GetAlternateIndexUp(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.GetUp(Common_Index_Button, ovrController);
        }

        public bool  IsThumbTouching(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);

            return OVRInput.Get(Common_Buttons_NearTouch, ovrController) || OVRInput.Get(Common_ThumbRest_Touch, ovrController);
        }

        // grip trigger values
        public float GetGripAmount(ControllerHand hand)
        {
            float amount = GetAxis1D(hand, Common_Grip_Axis);

            // analog trigger doesn't always return to zero when released
            if (amount < 0.01f)
                amount = 0.0f;

            return amount;
        }

        public bool  StartGrabbing(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.GetDown(Common_Grip_Button, ovrController);
        }

        public bool  ContinueGrabbing(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(Common_Grip_Button, ovrController);
        }

        public bool  ContinueGrabbingAlternate(ControllerHand hand, bool includeTouchContact = false)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(Common_Index_Button, ovrController) || (includeTouchContact && OVRInput.Get(Common_Index_Touch, ovrController));
        }

        public bool  StopGrabbing(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.GetUp(Common_Grip_Button, ovrController);
        }

        public bool ButtonAPress()
        {
            return OVRInput.Get(A_Button_Press);
        }

        public bool ButtonBPress()
        {
            return OVRInput.Get(B_Button_Press);
        }

        public bool ButtonXPress()
        {
            return OVRInput.Get(X_Button_Press);
        }

        public bool ButtonYPress()
        {
            return OVRInput.Get(Y_Button_Press);
        }

        public bool ThumbstickPress(ControllerHand hand)
        {
            var ovrController = ToOVRTouchHand(hand);
            return OVRInput.Get(Common_ThumbStick_Press, ovrController);
        }

        #endregion


        #region INTERFACE METHODS NOT USED WITH OCULUS TOUCH
        public float GetMiddleAmount(ControllerHand hand)       { return GetGripAmount(hand); }
        public bool IsMiddleNearTouching(ControllerHand hand)   { return false; }
        public bool IsMiddleTouching(ControllerHand hand)       { return false; }

        public float GetRingAmount(ControllerHand hand)         { return GetGripAmount(hand); }
        public bool IsRingNearTouching(ControllerHand hand)     { return false; }
        public bool IsRingTouching(ControllerHand hand)         { return false; }

        public float GetPinkyAmount(ControllerHand hand)        { return GetGripAmount(hand); }
        public bool IsPinkyNearTouching(ControllerHand hand)    { return false; }
        public bool IsPinkyTouching(ControllerHand hand)        { return false; }

        public float GetThumbAmount(ControllerHand hand)        { return 0; }
        public bool IsThumbNearTouching(ControllerHand hand)    { return false; }

        #endregion
    }
}
#endif