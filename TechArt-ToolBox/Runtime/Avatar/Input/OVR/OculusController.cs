using System.Collections;
using System.Collections.Generic;
using UnityEngine;

#if OVR_PLUGIN
namespace VRR.Input
{
    public class OculusControllerMapping
    {
        #region SINGLE CONTROLLER INPUTS 
        // primary values from OVRInput can be converted to reference the currently active hand controller
        // - this is good for defining general actions when using the "which hand is active" check 
        // - see the callback functions in the Input_TouchController as an example
        protected const OVRInput.NearTouch  Common_Index_NearTouch  = OVRInput.NearTouch.PrimaryIndexTrigger;
        protected const OVRInput.Touch      Common_Index_Touch      = OVRInput.Touch.PrimaryIndexTrigger;
        protected const OVRInput.Axis1D     Common_Index_Trigger    = OVRInput.Axis1D.PrimaryIndexTrigger;
        protected const OVRInput.Button     Common_Index_Button     = OVRInput.Button.PrimaryIndexTrigger;

        protected const OVRInput.Axis1D     Common_Grip_Axis        = OVRInput.Axis1D.PrimaryHandTrigger;
        protected const OVRInput.Button     Common_Grip_Button      = OVRInput.Button.PrimaryHandTrigger;

        protected const OVRInput.Axis2D     Common_ThumbStick_Axis  = OVRInput.Axis2D.PrimaryThumbstick;
        protected const OVRInput.Touch      Common_ThumbRest_Touch  = OVRInput.Touch.PrimaryThumbRest;
        protected const OVRInput.Button     Common_ThumbStick_Press = OVRInput.Button.PrimaryThumbstick;

        protected const OVRInput.NearTouch  Common_Buttons_NearTouch = OVRInput.NearTouch.PrimaryThumbButtons;
        #endregion

        //differentiating between right and left hands can give you more customizable options
        #region LEFT HAND INPUTS
        protected const OVRInput.NearTouch  Left_Index_NearTouch    = OVRInput.NearTouch.PrimaryIndexTrigger;
        protected const OVRInput.Touch      Left_Index_Touch        = OVRInput.Touch.PrimaryIndexTrigger;
        protected const OVRInput.Axis1D     Left_Index_Trigger      = OVRInput.Axis1D.PrimaryIndexTrigger;
        protected const OVRInput.Button     Left_Index_Button       = OVRInput.Button.PrimaryIndexTrigger;

        protected const OVRInput.Axis1D     Left_Grip_Trigger       = OVRInput.Axis1D.PrimaryHandTrigger;
        protected const OVRInput.Button     Left_Grip_Button        = OVRInput.Button.PrimaryHandTrigger;
        // no hand grip touch
        // no hand grip near touch

        protected const OVRInput.Touch      Left_Thumbstick_Touch   = OVRInput.Touch.PrimaryThumbstick;
        protected const OVRInput.Axis2D     Left_Thumbstick_Axis    = OVRInput.Axis2D.PrimaryThumbstick;
        protected const OVRInput.Button     Left_ThumbStick_Press   = OVRInput.Button.PrimaryThumbstick;

        protected const OVRInput.NearTouch  Left_Buttons_NearTouch  = OVRInput.NearTouch.PrimaryThumbButtons;
        protected const OVRInput.Touch      Left_ThumbRest_Touch    = OVRInput.Touch.PrimaryThumbRest;
        protected const OVRInput.Touch      Left_TouchPad_Touch     = OVRInput.Touch.PrimaryTouchpad;

        protected const OVRInput.Touch      X_Button_Touch          = OVRInput.Touch.Three;
        protected const OVRInput.Button     X_Button_Press          = OVRInput.Button.Three;

        protected const OVRInput.Touch      Y_Button_Touch          = OVRInput.Touch.Four;
        protected const OVRInput.Button     Y_Button_Press          = OVRInput.Button.Four;

        protected const OVRInput.Button     Start_Button_Press      = OVRInput.Button.Start;
        #endregion

        #region RIGHT HAND INPUTS
        protected const OVRInput.NearTouch  Right_Index_NearTouch   = OVRInput.NearTouch.SecondaryIndexTrigger;
        protected const OVRInput.Touch      Right_Index_Touch       = OVRInput.Touch.SecondaryIndexTrigger;
        protected const OVRInput.Axis1D     Right_Index_Trigger     = OVRInput.Axis1D.SecondaryIndexTrigger;
        protected const OVRInput.Button     Right_Index_Button      = OVRInput.Button.SecondaryIndexTrigger;

        protected const OVRInput.Axis1D     Right_Grip_Trigger      = OVRInput.Axis1D.SecondaryHandTrigger;
        protected const OVRInput.Button     Right_Grip_Button       = OVRInput.Button.SecondaryHandTrigger;
        // no hand grip touch
        // no hand grip near touch

        protected const OVRInput.Touch      Right_Thumbstick_Touch  = OVRInput.Touch.SecondaryThumbstick;
        protected const OVRInput.Axis2D     Right_Thumbstick_Axis   = OVRInput.Axis2D.SecondaryThumbstick;
        protected const OVRInput.Button     Right_ThumbStick_Press  = OVRInput.Button.SecondaryThumbstick;



        protected const OVRInput.NearTouch  Right_Buttons_NearTouch = OVRInput.NearTouch.SecondaryThumbButtons;
        protected const OVRInput.Touch      Right_ThumbRest_Touch   = OVRInput.Touch.SecondaryThumbRest;
        protected const OVRInput.Touch      Right_TouchPad_Touch    = OVRInput.Touch.SecondaryTouchpad;

        protected const OVRInput.Touch      A_Button_Touch          = OVRInput.Touch.One;
        protected const OVRInput.Button     A_Button_Press          = OVRInput.Button.One;

        protected const OVRInput.Touch      B_Button_Touch          = OVRInput.Touch.Two;
        protected const OVRInput.Button     B_Button_Press          = OVRInput.Button.Two;

        // Oculus Home Button is inacessable    
        #endregion

        #region CONTROLLER HANDEDNESS DETECTION
        // Dictionaries mapping out OVR's controller references to our enum
        // Oculus Rift uses the name Touch (eg LTouch and RTouch) 
        // while Oculus Go & Oculus Quest use the name TrackedRemote (eg LTrackedRemote and RTrackedRemote)
        private Dictionary<ControllerHand, OVRInput.Controller> ovrControllerMap = new Dictionary<ControllerHand, OVRInput.Controller>(new ControllerHandCompare())
        {
            { ControllerHand.Left, OVRInput.Controller.LTouch },
            { ControllerHand.Right, OVRInput.Controller.RTouch },
            { ControllerHand.Both, OVRInput.Controller.Touch },
            { ControllerHand.None, OVRInput.Controller.None },
        };

        private Dictionary<ControllerHand, OVRInput.Controller> ovrAndroidControllerMap = new Dictionary<ControllerHand, OVRInput.Controller>(new ControllerHandCompare())
        {
            { ControllerHand.Left, OVRInput.Controller.LTouch },
            { ControllerHand.Right, OVRInput.Controller.RTouch },
            { ControllerHand.Both, OVRInput.Controller.Remote },
            { ControllerHand.None, OVRInput.Controller.None }
        };

        public OVRInput.Controller ToOVRTouchHand(ControllerHand hand)
        {
            return ovrControllerMap[hand];
        }

        public OVRInput.Controller ToOVRAndroidHand(ControllerHand hand)
        {
            return ovrControllerMap[hand];
        }

        public ControllerHand GetOtherHand(ControllerHand hand)
        {
            return hand == ControllerHand.Left ? ControllerHand.Right : ControllerHand.Left;
        }

        private struct ControllerHandCompare : IEqualityComparer<ControllerHand>
        {
            public bool Equals(ControllerHand x, ControllerHand y)
            {
                return x == y;
            }

            public int GetHashCode(ControllerHand obj)
            {
                return (int)obj;
            }
        }
        #endregion
    }
}
#endif