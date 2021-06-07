using VRR.Input;

namespace VRR.Avatar.Emote
{
    public interface IFaceEmotes
    {
        bool IsHappy(ControllerHand hand);
        bool IsSad(ControllerHand hand);
        bool IsAngry(ControllerHand hand);
        bool IsSurprised(ControllerHand hand);
    }
}
