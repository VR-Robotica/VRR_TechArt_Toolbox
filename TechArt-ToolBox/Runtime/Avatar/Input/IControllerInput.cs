namespace VRR.Input
{
    /// <summary>
    /// Interface to setup access to common functions between all Input Controller Types (Oculus Touch, Vive Wands, Valve Index Controller, etc.)
    /// </summary>
    /// 
    public interface IControllerInput
    {
        bool IsControllerConnected(ControllerHand controllerHand);

        // Buttons and Sticks/Pads
        float GetHorizAxis(ControllerHand hand);
        float GetVertAxis(ControllerHand hand);

        bool Recentering();

        // Finger Triggers
        
        bool  IsIndexNearTouching(ControllerHand hand);
        bool  IsIndexTouching(ControllerHand hand);
        float GetIndexAmount(ControllerHand hand);

        // NOTE: 'Alternates' are used because the SDKs will switch controller dominance (aka primary) between the left and right hands
        bool GetAlternateIndexDown(ControllerHand hand);
        bool GetAlternateIndexUp(ControllerHand hand);
        bool GetAlternateIndex(ControllerHand hand);

        bool  IsMiddleNearTouching(ControllerHand hand);
        bool  IsMiddleTouching(ControllerHand hand);
        float GetMiddleAmount(ControllerHand hand);

        bool  IsRingNearTouching(ControllerHand hand);
        bool  IsRingTouching(ControllerHand hand);
        float GetRingAmount(ControllerHand hand);

        bool  IsPinkyNearTouching(ControllerHand hand);
        bool  IsPinkyTouching(ControllerHand hand);
        float GetPinkyAmount(ControllerHand hand);

        bool  IsThumbNearTouching(ControllerHand hand);
        bool  IsThumbTouching(ControllerHand hand);
        float GetThumbAmount(ControllerHand hand);

        // grip trigger (grouped fingers)
        float GetGripAmount(ControllerHand hand);

        bool  StartGrabbing(ControllerHand hand);
        bool  ContinueGrabbing(ControllerHand hand);
        bool  ContinueGrabbingAlternate(ControllerHand hand, bool includeTouchContact = false);
        bool  StopGrabbing(ControllerHand hand);

    }
}
