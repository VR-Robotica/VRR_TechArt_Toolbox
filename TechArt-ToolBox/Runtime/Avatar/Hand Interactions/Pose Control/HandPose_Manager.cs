using System.Collections;
using UnityEngine;
using VRR.Libraries;

namespace VRR.Avatar.Hands
{
    [System.Serializable]
    public class HandPose_Manager : MonoBehaviour
    {
        #pragma warning disable 0649
        [SerializeField] private HandPose_Controller    handPoseController;
        [SerializeField] private HandInteraction        handInteraction;
        [SerializeField] private Library_HandPoses      Library;
        [SerializeField] private float                  animationSpeed = 15.0f;
        #pragma warning restore 0649

        public bool IsPoseOverriding;

        // if this value changes, override the current pose...
        public  HandPose_Types HandPose
        {
            get { return _currentPose; }
            set
            {
                if(value != _currentPose)
                {
                    _currentPose = value;

                    RemoteOverride();
                }
            }
        }
        private HandPose_Types _currentPose = HandPose_Types.Free;

        #region MONOBEHAVIOR CALLBACKS
        private void OnEnable()
        {
            // this call just resets the pose
            RemoteOverride();
        }
       
        #endregion

        /// <summary>
        /// This manager overrides the input from the hand controller to animate the hand into defined poses
        /// </summary>
        private void RemoteOverride()
        {
            if (handPoseController == null) { return; }

            if(_currentPose != HandPose_Types.Free)
            { 
                HandPose_Asset pose = Library.GetPoseAsset(_currentPose);

                if(pose != null)
                {
                    Start_AnimatePose(pose);
                }
                else
                {
                    Stop_AnimatePose();
                }
            }
            else
            {
                Stop_AnimatePose();
            }
        }

        #region GETTERS
        public HandPose_Controller GetHandPoseController()
        {
            if (handPoseController != null) { SetHandPoseController(); }

            return handPoseController;
        }

        public Library_HandPoses GetPoseLibrary()
        {
            if (Library != null)
            {
                return Library;
            }

            Debug.LogWarning(this.name + " Library not set.");
            return null;
        }

        public HandPose_Asset GetPoseAsset(int index)
        {
            return Library.GetPoseAsset(index);
        }

        public HandPose_Asset GetPoseAsset(HandPose_Types pose)
        {
            return Library.GetPoseAsset(pose);
        }

        public HandPose_Types GetCurrentHandPose()
        {
            return _currentPose;
        }
        #endregion

        #region SETTERS
        private void SetHandPoseController()
        {
            if (handPoseController == null)
            {
                Debug.Log(this.gameObject.name + "'s Hand Controller Not Set. Attempting to locate component.");
                handPoseController = this.gameObject.GetComponent<HandPose_Controller>();
            }
        }

        public void SetHandPoseController(HandPose_Controller controller)
        {
            handPoseController = controller;
        }

        public void SetHandInteraction(HandInteraction interaction)
        {
            handInteraction = interaction;
        }

        public void SetLibrary(Library_HandPoses library)
        {
            Library = library;
        }

        public void SetHandPose(HandPose_Types pose)
        {
            if (pose != _currentPose)
            {
                _currentPose = pose;
                RemoteOverride();
            }
        }
        public void SetGrabbing(bool value)
        {
            handInteraction.SetGrabbingValue(value);

            IsPoseOverriding = handInteraction.IsGrabbingSomething();

            if (IsPoseOverriding)
            {
                SetHandPose(handInteraction.GetPose());
            }
            else
            {
                SetHandPose(HandPose_Types.Free);
            }
        }
        public void SetAnimationSpeed(float amount)
        {
            animationSpeed = amount;
        }
        #endregion

        #region POSE ANIMATION COROUTINES
        private void Start_AnimatePose(HandPose_Asset pose)
        {
            Stop_AnimatePose();
            AnimatePose_coroutine = StartCoroutine(AnimatePose(pose));
        }

        private void Stop_AnimatePose()
        {
            if (AnimatePose_coroutine != null)
            {
                StopCoroutine(AnimatePose_coroutine);
                AnimatePose_coroutine = null;
            }
        }

        private Coroutine AnimatePose_coroutine;
        private IEnumerator AnimatePose(HandPose_Asset pose)
        {
            if (handPoseController != null) { SetHandPoseController(); }

            float[] fingerBaseCurls = pose.GetFingerBaseCurlValues();
            float[] fingerTipCurls = pose.GetFingerTipCurlValues();
            float[] thumbBaseCurls = pose.GetThumbBaseCurlValues();
            float[] thumbTipCurls = pose.GetThumbTipCurlValues();

            float speed = animationSpeed * Time.deltaTime;

            while (true)
            {
                for (int i = 0; i < handPoseController.Hand.GetFingerLength(); i++)
                {
                    handPoseController.FingerBaseCurlAmounts[i] = Mathf.Lerp(handPoseController.FingerBaseCurlAmounts[i], fingerBaseCurls[i], speed);
                    handPoseController.FingerTipCurlAmounts[i] = Mathf.Lerp(handPoseController.FingerTipCurlAmounts[i], fingerTipCurls[i], speed);
                }

                for (int i = 0; i < handPoseController.Hand.GetThumbLength(); i++)
                {
                    handPoseController.ThumbBaseCurlAmounts[i] = Mathf.Lerp(handPoseController.ThumbBaseCurlAmounts[i], thumbBaseCurls[i], speed);
                    handPoseController.ThumbTipCurlAmounts[i] = Mathf.Lerp(handPoseController.ThumbTipCurlAmounts[i], thumbTipCurls[i], speed);
                }

                yield return null;
            }
        }
        #endregion
    }
}