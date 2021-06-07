using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.Avatar.Emote
{
    [System.Serializable]
    public class FaceEmote_Manager : MonoBehaviour
    {
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649
        [SerializeField] private SkinnedMeshRenderer SkinnedMesh;
        [SerializeField] private FaceEmote_Controller EmoteController;
        [SerializeField] private float AnimationSpeed = 15.0f;
        [SerializeField] private FaceEmote_Asset[] EmoteLibrary;
        [HideInInspector] public bool IsReady { get; private set; }
        #pragma warning restore 0649
        #endregion

        // if this value changes, change the pose...?
        public FaceEmote_Types FaceEmote
        {
            get { return _currentEmote; }
            set
            {
                if (value != _currentEmote)
                {
                    _currentEmote = value;
                    OverrideFaceEmote();
                }
            }
        }
        private FaceEmote_Types _currentEmote = FaceEmote_Types.Free;



        #region MONOBEHAVIOR CALLBACKS
        private void Awake()
        {
            EmoteController.SetSkinnedMesh(SkinnedMesh);
            IsReady = true;
        }

        private void Start()
        {
            OverrideFaceEmote();
        }

        private void OnEnable()
        {
            // this call just resets the pose
            OverrideFaceEmote();
        }
        #endregion

        /// <summary>
        /// This manager overrides the input from the hand controller to animate the hand into defined poses
        /// </summary>
        private void OverrideFaceEmote()
        {
            if (EmoteController != null)
            {
                for (int i = 0; i < EmoteLibrary.Length; i++)
                {
                    if (_currentEmote == EmoteLibrary[i].GetFaceEmoteType())
                    {
                        Start_AnimateEmote(EmoteLibrary[i]);
                        return;
                    }
                    else
                    {
                        Stop_AnimateEmote();
                    }
                }
            }
        }

        #region POSE ANIMATION COROUTINES
        private void Start_AnimateEmote(FaceEmote_Asset emote)
        {
            Stop_AnimateEmote();
            AnimateEmote_coroutine = StartCoroutine(AnimateEmote(emote));
        }

        private void Stop_AnimateEmote()
        {
            if (AnimateEmote_coroutine != null)
            {
                StopCoroutine(AnimateEmote_coroutine);
                AnimateEmote_coroutine = null;
                ResetEmotes();
            }
        }

        private void ResetEmotes()
        {
            EmoteController.ResetBlendshapes();
        }

        private Coroutine AnimateEmote_coroutine;
        private IEnumerator AnimateEmote(FaceEmote_Asset emote)
        {
            float speed = AnimationSpeed * Time.deltaTime;

            if (emote.UsingProperties())
            {
                Dictionary<int, int> emoteDictionary = emote.Properties.GetValues();
                List<int> blendshapeIndex            = GetAllKeys(emoteDictionary);
                List<int> targetValues               = GetAllValues(emoteDictionary);
                int[] currentValues                  = new int[emoteDictionary.Count];

                while (true)
                {
                    // lerp amounts from current to target
                    for (int i = 0; i < currentValues.Length; i++)
                    {
                        currentValues[i] = Mathf.RoundToInt(Mathf.Lerp(currentValues[i], targetValues[i], speed));
                    }

                    // set blendshape indices to new current amounts
                    for (int i = 0; i < currentValues.Length; i++)
                    {
                        EmoteController.SetBlendShapeValues(blendshapeIndex[i], currentValues[i]);
                    }

                    yield return null;
                }
            }
            else
            {
                Vector2Int leftValues = emote.GetLeftEmoteCoordinates();
                Vector2Int rightValues = emote.GetRightEmoteCoordinates();

                Vector2Int currentLeft = Vector2Int.zero;
                Vector2Int currentRight = Vector2Int.zero;

                while (currentLeft != leftValues && currentRight != rightValues)
                {
                    currentLeft = new Vector2Int(   Mathf.RoundToInt(Mathf.Lerp(currentLeft.x, leftValues.x, speed)),
                                                    Mathf.RoundToInt(Mathf.Lerp(currentLeft.y, leftValues.y, speed))
                                                    );

                    currentRight = new Vector2Int(  Mathf.RoundToInt(Mathf.Lerp(currentRight.x, rightValues.x, speed)),
                                                    Mathf.RoundToInt(Mathf.Lerp(currentRight.y, rightValues.y, speed))
                                                    );

                    EmoteController.SetBlendShapeValues(currentLeft.x, currentLeft.y);


                    yield return null;

                }
            }
        }
        #endregion
        
        bool CheckEquality(int[] current, List<int> target)
        {
            for(int i = 0; i < target.Count; i++)
            {
                if(current[i] != target[i])
                {
                    return false;
                }
            }

            return true;
        }

        #region GETTERS
        public FaceEmote_Asset[] GetEmoteLibrary()
        {
            return EmoteLibrary;
        }

        public FaceEmote_Asset GetEmoteAsset(int index)
        {
            return EmoteLibrary[index];
        }

        public FaceEmote_Asset GetEmoteValues(FaceEmote_Types emoteType)
        {
            if (EmoteLibrary.Length > 0)
            {
                for (int i = 0; i < EmoteLibrary.Length; i++)
                {
                    if (emoteType == EmoteLibrary[i].GetFaceEmoteType())
                    {
                        return EmoteLibrary[i];
                    }
                }
            }
            else
            {
                Debug.LogWarning("Pose library not defined in editor.");
            }

            Debug.Log("Pose Asset for [" + emoteType.ToString() + "] was not added to the library in the editor.");
            return null;
        }

        public FaceEmote_Types GetFaceEmote()
        {
            return _currentEmote;
        }

        private List<int> GetAllKeys(Dictionary<int, int> dict)
        {
            List<int> keys = new List<int>();

            foreach (KeyValuePair<int, int> kvp in dict)
            {
                keys.Add(kvp.Key);
            }

            return keys;
        }

        private List<int> GetAllValues(Dictionary<int, int> dict)
        {
            List<int> values = new List<int>();

            foreach (KeyValuePair<int, int> kvp in dict)
            {
                values.Add(kvp.Value);
            }

            return values;
        }

        #endregion

        #region SETTERS
        private void SetFaceEmoteController()
        {
            if (EmoteController == null)
            {
                Debug.Log(this.gameObject.name + "'s Emote Controller Not Set. Attempting to locate component.");
                EmoteController = this.gameObject.GetComponent<FaceEmote_Controller>();
            }
        }

        public void SetFaceEmote(FaceEmote_Types type)
        {
            if (type != _currentEmote)
            {
                _currentEmote = type;
                OverrideFaceEmote();
            }
        }
        #endregion
    }
}