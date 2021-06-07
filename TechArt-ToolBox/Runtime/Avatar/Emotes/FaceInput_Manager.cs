using UnityEngine;
using VRR.Input;

using VRR.Avatar.Blendshape;

namespace VRR.Avatar.Emote
{
    /// <summary>
    /// This takes in the axis input values from the Oculus Touch Controllers and passes them on to the blendshape controllers
    /// </summary>
    public class FaceInput_Manager : MonoBehaviour
    {
        #pragma warning disable 0649

        [SerializeField] private FaceEmote_Manager FaceEmoteManager;
        [Space]
        [SerializeField] private BlendShape_Controller leftControl;
        [SerializeField] private BlendShape_Controller rightControl;
        float AnimationSpeed = 15.0f;
        
        #pragma warning restore 0649

        private IControllerInput _leftControllerInput;
        private IControllerInput _rightControllerInput;

        [HideInInspector] public bool IsReady { get; private set; }

        #region MONOBEHAVIOR CALLBACKS
       
        void Update()
        {
            if (_leftControllerInput != null && _rightControllerInput != null)
            {
                // get axis values from each controller
                UpdateAxisInput(ControllerHand.Left);
                UpdateAxisInput(ControllerHand.Right);
            }
        }
        
        #endregion

        private void UpdateAxisInput(ControllerHand hand)
        {
            Vector2Int axis = Vector2Int.zero;

            // pass left/right values to coressponding blendshape controllers
            switch (hand)
            {
                case ControllerHand.Left:
                    //axis = ConvertToIntValues(_leftControllerInput.GetThumbstickAxis(hand));
                    SetControllerValue(axis, leftControl);

                    //Debug.Log("Left Input: " + axis);
                    break;

                case ControllerHand.Right:
                    //axis = ConvertToIntValues(_rightControllerInput.GetThumbstickAxis(hand));
                    SetControllerValue(axis, rightControl);

                    //Debug.Log("Right Input: " + axis);
                    break;
            }
        }

        private Vector2Int ConvertToIntValues(Vector2 axis)
        {
            // convert input float values (0-1) to int values (0-100) 
            // - Unity's Blendshapes use 0-100 and it's nicer for networking
            int currentX = Mathf.Clamp(Mathf.RoundToInt(axis.x * 100), -100, 100);
            int currentY = Mathf.Clamp(Mathf.RoundToInt(axis.y * 100), -100, 100);

            return new Vector2Int(currentX, currentY);
        }

        public void SetControllerInputs(IControllerInput left, IControllerInput right)
        {
            #if OVR_PLUGIN
            _leftControllerInput = new Input_OculusController();
            _rightControllerInput = new Input_OculusController();
            #endif
        }

        private void SetControllerValue(Vector2Int position, BlendShape_Controller controller)
        { 
            if (controller != null)
            {
                controller.SetBlendShapeValues(position.x, position.y);
            }
            else
            {
                Debug.LogWarning("BlendShape Controller not set in Editor");
            }
        }
    }
}