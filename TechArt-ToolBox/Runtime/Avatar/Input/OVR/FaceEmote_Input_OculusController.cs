using VRR.Input;

namespace VRR.Avatar.Emote
{
    public class FaceEmote_Input_OculusController : Input_OculusController, IFaceEmotes
    {
        private const float MAX_THRESHOLD = 0.5f;
        private const float MIN_THRESHOLD = 0.1f;

        // add custom info here 
        // - see HandInput_TouchController as an example

        public bool IsHappy(ControllerHand hand)
        {
            return GetHorizAxis(hand) > MAX_THRESHOLD;
        }

        public bool IsSad(ControllerHand hand)
        {
            return GetHorizAxis(hand) < -MAX_THRESHOLD;
        }

        public bool IsAngry(ControllerHand hand)
        {
            return GetVertAxis(hand) < -MAX_THRESHOLD;
        }

        public bool IsSurprised(ControllerHand hand)
        {
            return GetVertAxis(hand) > MAX_THRESHOLD;
        }
    }
}