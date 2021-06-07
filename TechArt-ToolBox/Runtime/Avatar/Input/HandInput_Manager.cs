using UnityEngine;
using VRR.Input;

namespace VRR.Avatar.Hands
{
    /// <summary>
    /// Updates the Input values coming in from the controllers
    /// </summary>
    public class HandInput_Manager : MonoBehaviour
    {
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649
        [SerializeField] private HandPose_Manager leftPoseManager;
        [SerializeField] private HandPose_Manager rightPoseManager;
        [SerializeField] private float AnimationSpeed = 15.0f;
        #pragma warning restore 0649
        #endregion

        private IHandControllerInput _handInput;

        #region PROPERTIES
        private float _nearTouch_LeftTriggerMultiplier  = 0.0f;
        private float _nearTouch_RightTriggerMultiplier = 0.0f;
        private float _touch_leftThumbMultiplier        = 0.0f;
        private float _touch_rightThumbMultiplier       = 0.0f;
        private float _triggerTouchInfluence            = 0.2f;
        private float _currentLeftTriggerAmount;
        private float _currentLeftGripAmount;
        private float _currentRightTriggerAmount;
        private float _currentRightGripAmount;

        private float _combinedLeftInputAmounts;
        private float _combinedRightInputAmounts;
        private float _leftInfluenceMultiplier;
        private float _rightInfluenceMultiplier;
        
        private float rate;

        private HandPose_Manager    _poseManager;
        private HandPose_Controller _handPoseController;
        private HandPose_Asset      _targetPose;
        #endregion

        #region CONSTANTS
        private float LERP_CLAMP_MIN = 0.01f;
        private float LERP_CLAMP_MAX = 0.99f;

        private float ANALOG_CLAMP_MIN = 0.02f;
        private float ANALOG_CLAMP_MAX = 0.98f;
        #endregion

        #region MONOBEHAVIOR CALLBACKS
        private void Awake()
        {
            leftPoseManager.SetAnimationSpeed(AnimationSpeed);
            rightPoseManager.SetAnimationSpeed(AnimationSpeed);
        }

        private void Update()
        {
            if (_handInput != null)
            {
                UpdateInputs();
                UpdateHandPoseManager();
            }
        }

        #endregion

        #region GETTERS
        public HandPose_Manager GetPoseManager(ControllerHand hand)
        {
            if (hand == ControllerHand.Right) return rightPoseManager;
            else return leftPoseManager;
        }
        #endregion

        #region SETTERS

        public void SetHandInput(IHandControllerInput input)
        {
            #if OVR_PLUGIN
            if (input.GetType() == typeof(HandPose_Input_OculusController))
            {
                _handInput = new HandPose_Input_OculusController();
            }
            #endif
        }

        public void SetPoseManager(ControllerHand hand, HandPose_Manager manager)
        {
            switch (hand)
            {
                case ControllerHand.Left:
                    leftPoseManager = manager;
                    break;

                case ControllerHand.Right:
                    rightPoseManager = manager;
                    break;
            }
        }
       
        private void SetAnimationSpeed()
        {
            //AnimationSpeed = leftPoseManager.get
        }

        public void SetPose(ControllerHand hand, HandPose_Types pose)
        {
            if (hand == ControllerHand.Left) leftPoseManager.SetHandPose(pose);
            else if (hand == ControllerHand.Right) rightPoseManager.SetHandPose(pose);
        }

        #endregion

        #region UPDATE SEQUENCE
        private void UpdateInputs()
        {
            // a. make sure we have a pose manager
            // b. make sure the pose manage is active
            // c. make sure the pose manager is not set to override the inputs

            if(leftPoseManager != null && leftPoseManager.gameObject.activeInHierarchy)
            {
                UpdateThumbTouches(ControllerHand.Left);
                UpdateTriggerTouches(ControllerHand.Left);
                UpdateIndexTrigger(ControllerHand.Left);
                UpdateGripTrigger(ControllerHand.Left);
            }

            if(rightPoseManager != null && rightPoseManager.gameObject.activeInHierarchy)
            {
                UpdateTriggerTouches(ControllerHand.Right);
                UpdateThumbTouches(ControllerHand.Right);
                UpdateIndexTrigger(ControllerHand.Right);
                UpdateGripTrigger(ControllerHand.Right);
            }
        }

        private void UpdateGrabCheck()
        {
            if (leftPoseManager != null)
            {
                leftPoseManager.SetGrabbing(_handInput.IsGrabbing(ControllerHand.Left));
            }

            if (rightPoseManager != null)
            {
                rightPoseManager.SetGrabbing(_handInput.IsGrabbing(ControllerHand.Right));
            }
        }

        private void UpdateHandPoseManager()
        {
            UpdateGrabCheck();

            if (leftPoseManager != null && leftPoseManager.gameObject.activeInHierarchy && !leftPoseManager.IsPoseOverriding)
            {
                UpdateIndexFinger(ControllerHand.Left);
                UpdateLowerFingers(ControllerHand.Left);
                UpdateThumb(ControllerHand.Left);
            }

            if (rightPoseManager != null && rightPoseManager.gameObject.activeInHierarchy && !rightPoseManager.IsPoseOverriding)
            {
                UpdateIndexFinger(ControllerHand.Right);
                UpdateLowerFingers(ControllerHand.Right);
                UpdateThumb(ControllerHand.Right);
            }

        }

        #region UPDATE BOOLEAN INPUTS
       
        private void UpdateThumbTouches(ControllerHand hand)
        {
            rate = Time.deltaTime * AnimationSpeed;

            if (CheckHandReadiness(hand))
            {
                if (_handInput.IsThumbTouching(hand))
                {
                    // when touching, adjust multiplier value to 1
                    switch (hand)
                    {
                        case ControllerHand.Left:
                            _touch_leftThumbMultiplier = LerpMultiplier(_touch_leftThumbMultiplier, 1.0f); 
                            break;
                        case ControllerHand.Right:
                            _touch_rightThumbMultiplier = LerpMultiplier(_touch_rightThumbMultiplier, 1.0f); 
                            break;
                    }

                    // clamp values
                    if (_touch_leftThumbMultiplier > LERP_CLAMP_MAX)  { _touch_leftThumbMultiplier = 1; }
                    if (_touch_rightThumbMultiplier > LERP_CLAMP_MAX) { _touch_rightThumbMultiplier = 1; }
                }
                else
                {
                    // when NOT touching, adjust multiplier value to 0
                    switch (hand)
                    {
                        case ControllerHand.Left:
                            _touch_leftThumbMultiplier = LerpMultiplier(_touch_leftThumbMultiplier, 0.0f); 
                            break;
                        case ControllerHand.Right:
                            _touch_rightThumbMultiplier = LerpMultiplier(_touch_rightThumbMultiplier, 0.0f);
                            break;
                    }

                    // clamp values
                    if (_touch_leftThumbMultiplier < LERP_CLAMP_MIN)  { _touch_leftThumbMultiplier = 0; }
                    if (_touch_rightThumbMultiplier < LERP_CLAMP_MIN) { _touch_rightThumbMultiplier = 0; }
                }
            }
        }

        private void UpdateTriggerTouches(ControllerHand hand)
        {
            rate = Time.deltaTime * AnimationSpeed;

            if (CheckHandReadiness(hand))
            {
                if (_handInput.IsIndexNearTouching(hand))
                {
                    // when touching, adjust multiplier value to 1
                    switch (hand)
                    {
                        case ControllerHand.Left:
                            _nearTouch_LeftTriggerMultiplier = LerpMultiplier(_nearTouch_LeftTriggerMultiplier, 1.0f); 
                            break;
                        case ControllerHand.Right:
                            _nearTouch_RightTriggerMultiplier = LerpMultiplier(_nearTouch_RightTriggerMultiplier, 1.0f);
                            break;
                    }

                    // clamp values
                    if (_nearTouch_LeftTriggerMultiplier > LERP_CLAMP_MAX)  { _nearTouch_LeftTriggerMultiplier  = 1; }
                    if (_nearTouch_RightTriggerMultiplier > LERP_CLAMP_MAX) { _nearTouch_RightTriggerMultiplier = 1; }
                }
                else
                {
                    // when NOT touching, adjust multiplier value to 0
                    switch (hand)
                    {
                        case ControllerHand.Left:
                            _nearTouch_LeftTriggerMultiplier = LerpMultiplier(_nearTouch_LeftTriggerMultiplier, 0.0f);
                            break;
                        case ControllerHand.Right:
                            _nearTouch_RightTriggerMultiplier = LerpMultiplier(_nearTouch_RightTriggerMultiplier, 0.0f);
                            break;
                    }

                    // clamp values
                    if (_nearTouch_LeftTriggerMultiplier < LERP_CLAMP_MIN)  { _nearTouch_LeftTriggerMultiplier  = 0; }
                    if (_nearTouch_RightTriggerMultiplier < LERP_CLAMP_MIN) { _nearTouch_RightTriggerMultiplier = 0; }
                }
            }
        }
        #endregion

        #region UPDATE ANALOG INPUTS
        private void UpdateIndexTrigger(ControllerHand hand)
        {
            if (CheckHandReadiness(hand))
            {
                switch (hand)
                {
                    case ControllerHand.Left:
                        _currentLeftTriggerAmount  = Mathf.Round(_handInput.GetIndexAmount(hand) * 100)/100;

                        if (_currentLeftTriggerAmount < ANALOG_CLAMP_MIN){ _currentLeftTriggerAmount = 0f; }
                        if (_currentLeftTriggerAmount > ANALOG_CLAMP_MAX){ _currentLeftTriggerAmount = 1f; }

                        _combinedLeftInputAmounts = Mathf.Clamp(_currentLeftTriggerAmount + (_currentLeftGripAmount * _nearTouch_LeftTriggerMultiplier) + (_nearTouch_LeftTriggerMultiplier * _triggerTouchInfluence), 0, 1);
                        _leftInfluenceMultiplier = Mathf.Clamp((1 + (_touch_leftThumbMultiplier * (1 - _currentLeftGripAmount))) - (_touch_leftThumbMultiplier), 1, 2);
                        break;

                    case ControllerHand.Right:
                        _currentRightTriggerAmount = Mathf.Round(_handInput.GetIndexAmount(hand)*100)/100;

                        if (_currentRightTriggerAmount < LERP_CLAMP_MIN) { _currentRightTriggerAmount = 0f; }
                        if (_currentRightTriggerAmount > LERP_CLAMP_MAX) { _currentRightTriggerAmount = 1f; }

                        _combinedRightInputAmounts = Mathf.Clamp(_currentRightTriggerAmount + (_currentRightGripAmount * _nearTouch_RightTriggerMultiplier) + (_nearTouch_RightTriggerMultiplier * _triggerTouchInfluence), 0, 1);
                        _rightInfluenceMultiplier = Mathf.Clamp((1 + (_touch_rightThumbMultiplier * (1 - _currentRightGripAmount))) - (_touch_rightThumbMultiplier), 1, 2);
                        break;
                }
            }
        }

        private void UpdateGripTrigger(ControllerHand hand)
        {
            _poseManager                 = null;
            float   currentGripAmount   = 0.0f;

            if (CheckHandReadiness(hand))
            {
                currentGripAmount = Mathf.Round(_handInput.GetGripAmount(hand)*100)/100;

                if (currentGripAmount < ANALOG_CLAMP_MIN){ currentGripAmount = 0f; }
                if (currentGripAmount > ANALOG_CLAMP_MAX){ currentGripAmount = 1f; }

                switch (hand)
                {
                    case ControllerHand.Left:
                        _poseManager            = leftPoseManager;
                        _currentLeftGripAmount  = currentGripAmount;
                        break;
                    case ControllerHand.Right:
                        _poseManager            = rightPoseManager;
                        _currentRightGripAmount = currentGripAmount;
                        break;
                }
            }
            else
            {
                Debug.LogWarning("UpdateGripTrigger(" + hand + ") - is not ready.");
            }
        }
        #endregion

        #region UPDATE Hand Controller Values
        private void UpdateThumb(ControllerHand hand)
        {
            if (CheckHandReadiness(hand))
            {
                // get hand refs...
                _poseManager         = leftPoseManager;
                float multiplier    = _touch_leftThumbMultiplier;
                float thumbTipCurl  = (_nearTouch_LeftTriggerMultiplier * 0.1f) + (_currentLeftGripAmount * 0.9f);
                float gripAmount    = _currentLeftGripAmount;
                float triggerAmount = _currentLeftTriggerAmount;

                rate = Time.deltaTime * AnimationSpeed;

                if (hand == ControllerHand.Right)
                {
                    _poseManager     = rightPoseManager;
                    multiplier      = _touch_rightThumbMultiplier;
                    thumbTipCurl    = (_nearTouch_RightTriggerMultiplier * 0.1f) + (_currentRightGripAmount * 0.9f);
                    gripAmount      = _currentRightGripAmount;
                    triggerAmount   = _currentRightTriggerAmount;
                }

                _handPoseController = _poseManager.GetHandPoseController();

                // get target values...
                int length              = _poseManager.GetHandPoseController().ThumbBaseCurlAmounts.Length;
                float[] targetBaseCurl  = new float[length];
                float[] targetTipCurl   = new float[length];

                if (_handInput.IsThumbsUp(hand) || _handInput.IsPointing_ThumbUp(hand)) // Animate Target Pose Override
                {
                    HandPose_Asset targetPose = _poseManager.GetPoseAsset(HandPose_Types.Thumb_Up);

                    for (int i = 0; i < length; i++)
                    {
                        targetBaseCurl[i] = gripAmount * targetPose.GetThumbBaseCurlValue(i);
                        targetTipCurl[i]  = gripAmount * targetPose.GetThumbTipCurlValue(i);
                    }
                }
                else
                if (_handInput.IsOkaySign(hand)) // Animate Target Pose Override
                {
                    HandPose_Asset targetPose = _poseManager.GetPoseAsset(HandPose_Types.Okay);

                    for (int i = 0; i < length; i++)
                    {
                        targetBaseCurl[i] = triggerAmount * targetPose.GetThumbBaseCurlValue(i);
                        targetTipCurl[i]  = triggerAmount * targetPose.GetThumbTipCurlValue(i);
                    }
                }
                else // reset values back to 0
                {
                    for (int i = 0; i < length; i++)
                    {
                        targetBaseCurl[i] = multiplier;
                        targetTipCurl[i] = thumbTipCurl;
                    }
                }

                // animate to target values...
                for (int i = 0; i < _poseManager.GetHandPoseController().ThumbBaseCurlAmounts.Length; i++)
                {
                    _handPoseController.ThumbBaseCurlAmounts[i] = Mathf.Lerp(_handPoseController.ThumbBaseCurlAmounts[i], targetBaseCurl[i], rate);
                    _handPoseController.ThumbTipCurlAmounts[i]  = Mathf.Lerp(_handPoseController.ThumbTipCurlAmounts[i],  targetTipCurl[i],  rate);
                }
            }
        }

        private void UpdateIndexFinger(ControllerHand hand)
        {
            if (CheckHandReadiness(hand))
            {
                // get hand refs...
                _poseManager = leftPoseManager;
                float combinedInputAmounts  = _combinedLeftInputAmounts;
                float influenceMultiplier   = _leftInfluenceMultiplier;
                float gripAmount            = _currentLeftGripAmount;
                float triggerAmount         = _currentLeftTriggerAmount;

                rate = Time.deltaTime * AnimationSpeed;

                if (hand == ControllerHand.Right)
                {
                    _poseManager             = rightPoseManager;
                    combinedInputAmounts    = _combinedRightInputAmounts;
                    influenceMultiplier     = _rightInfluenceMultiplier;
                    gripAmount              = _currentRightGripAmount;
                    triggerAmount           = _currentRightTriggerAmount;
                }

                _handPoseController = _poseManager.GetHandPoseController();

                // get target values...
                float targetBaseCurl = 0.0f;
                float targetTipCurl = 0.0f;

                if (_handInput.IsPointing(hand) || _handInput.IsPointing_ThumbUp(hand))
                {
                    _targetPose = _poseManager.GetPoseAsset(HandPose_Types.IndexPoint);

                    targetBaseCurl = gripAmount * _targetPose.GetFingerBaseCurlValue(0);
                    targetTipCurl  = gripAmount * _targetPose.GetFingerTipCurlValue(0);
                }
                else
                if (_handInput.IsOkaySign(hand)) // Animate Target Pose Override
                {
                    _targetPose = _poseManager.GetPoseAsset(HandPose_Types.Okay);

                    targetBaseCurl = triggerAmount * _targetPose.GetFingerBaseCurlValue(0);
                    targetTipCurl  = triggerAmount * _targetPose.GetFingerTipCurlValue(0);
                }
                else
                if (_handInput.IsThumbTouching(hand)) // check for thumb touch influence
                {
                    targetBaseCurl = combinedInputAmounts / influenceMultiplier;
                    targetTipCurl = combinedInputAmounts / influenceMultiplier;
                }
                else
                {
                    targetBaseCurl = combinedInputAmounts;
                    targetTipCurl  = combinedInputAmounts;
                }
                
                // animate to target values...
                _handPoseController.FingerBaseCurlAmounts[0] = Mathf.Lerp(_handPoseController.FingerBaseCurlAmounts[0], targetBaseCurl, rate);
                _handPoseController.FingerTipCurlAmounts[0]  = Mathf.Lerp(_handPoseController.FingerTipCurlAmounts[0],  targetTipCurl,  rate);
            }
        }

        private void UpdateLowerFingers(ControllerHand hand)
        {
            if (CheckHandReadiness(hand))
            {
                // get hand refs...
                HandPose_Manager poseManager = leftPoseManager;
                float nearTouchMultiplier   = _nearTouch_LeftTriggerMultiplier;
                float gripAmount            = _currentLeftGripAmount;
                float triggerAmount         = _currentLeftTriggerAmount;

                rate = Time.deltaTime * AnimationSpeed;

                if(hand == ControllerHand.Right)
                {
                    poseManager             = rightPoseManager;
                    nearTouchMultiplier     = _nearTouch_RightTriggerMultiplier;
                    gripAmount              = _currentRightGripAmount;
                    triggerAmount           = _currentRightTriggerAmount;
                }

                HandPose_Controller handPoseController = poseManager.GetHandPoseController();

                // get target values...
                int length = handPoseController.FingerBaseCurlAmounts.Length;
                float[] targetBaseCurl = new float[length];
                float[] targetTipCurl = new float[length];

                if (_handInput.IsOkaySign(hand))
                {
                    HandPose_Asset targetPose = poseManager.GetPoseAsset(HandPose_Types.Okay);

                    for (int i = 0; i < length; i++)
                    {
                        targetBaseCurl[i] = triggerAmount * targetPose.GetFingerBaseCurlValue(i);
                        targetTipCurl[i] = triggerAmount * targetPose.GetFingerTipCurlValue(i);
                    }
                }
                else
                {
                    if (gripAmount < _triggerTouchInfluence)
                    {
                        for (int i = 0; i < length; i++)
                        {
                            targetBaseCurl[i] = (gripAmount * (1 - _triggerTouchInfluence)) + (_triggerTouchInfluence * nearTouchMultiplier) * -1.0f;
                            targetTipCurl[i]  = (gripAmount * (1 - _triggerTouchInfluence)) + (_triggerTouchInfluence * nearTouchMultiplier) * -1.0f;
                        }
                    }
                    else
                    {
                        for (int i = 0; i < length; i++)
                        {
                            targetBaseCurl[i] = gripAmount;
                            targetTipCurl[i]  = gripAmount;
                        }
                    }
                }

                // animate to target values...
                for (int i = 1; i < handPoseController.FingerBaseCurlAmounts.Length; i++)
                {
                    handPoseController.FingerBaseCurlAmounts[i] = Mathf.Lerp(handPoseController.FingerBaseCurlAmounts[i], targetBaseCurl[i], rate);
                    handPoseController.FingerTipCurlAmounts[i]  = Mathf.Lerp(handPoseController.FingerTipCurlAmounts[i],  targetTipCurl[i],  rate);
                }
            }
        }
        #endregion

        #endregion

        #region HELPERS
        private bool CheckHandReadiness(ControllerHand hand)
        {
            switch (hand)
            {
                case ControllerHand.Left:
                    if (leftPoseManager != null && 
                        leftPoseManager.GetHandPoseController() != null)// && leftPoseManager.GetHandPose() == HandPose_Types.Free)
                    {
                        return true;
                    }
                    break;

                case ControllerHand.Right:
                    if (rightPoseManager != null && 
                        rightPoseManager.GetHandPoseController() != null)// && rightPoseManager.GetHandPose() == HandPose_Types.Free)
                    {
                        return true;
                    }
                    break;
            }
            return false;
        }

        /// <summary>
        /// This Lerp function helps to 'animates' the value of the multipliers over time
        /// This way the boolean style touches & near touches have a smooth effect
        /// </summary>
        /// <param name="multiplier"></param>
        /// <param name="targetValue"></param>
        /// <returns></returns>
        private float LerpMultiplier(float multiplier, float targetValue)
        {
            return Mathf.Round(Mathf.Lerp(multiplier, targetValue, rate) * 100) / 100;
        }
        #endregion
    }
}