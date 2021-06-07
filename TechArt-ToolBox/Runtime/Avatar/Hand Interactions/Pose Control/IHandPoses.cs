using VRR.Input;

namespace VRR.Avatar.Hands
{
    public interface IHandPoses
    {
        /// NOTE: These bool callbacks should closely align with the options in the enum 'HandPoseTypes'
        /// 

        bool IsPosing(ControllerHand hand);
        bool IsGrabbing(ControllerHand hand);
        bool IsResting(ControllerHand hand);
        bool IsFist(ControllerHand hand);
        bool IsPointing(ControllerHand hand);
        bool IsPointing_ThumbUp(ControllerHand hand);
        bool IsThumbsUp(ControllerHand hand);
        bool IsOkaySign(ControllerHand hand);
    }
}
