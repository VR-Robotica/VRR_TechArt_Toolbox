using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.Avatar.Emote
{
    [System.Serializable]
    public struct EmoteProperties
    {
        /// <summary>
        /// The Emote Properties Struct is where we define our skinned mesh reference and  
        /// the blendshapes that we will use to define the emotional 'pose' of our character's emote asset
        /// </summary>
        
        public SkinnedMeshRenderer SkinnedMesh;
        
        public int Brow_Up_L, Brow_Up_L_Value;
        public int Brow_Up_C, Brow_Up_C_Value;
        public int Brow_Up_R, Brow_Up_R_Value;

        public int Brow_Down_L, Brow_Down_L_Value;
        public int Brow_Down_C, Brow_Down_C_Value;
        public int Brow_Down_R, Brow_Down_R_Value;

        public int Cheek_L, Cheek_L_Value;
        public int Cheek_R, Cheek_R_Value;

        public int Mouth_Up, Mouth_Up_Value;
        public int Mouth_Down, Mouth_Down_Value;
        public int Mouth_L, Mouth_L_Value;
        public int Mouth_R, Mouth_R_Value;

        public Dictionary<int, int> GetValues()
        {
            Dictionary<int, int> values = new Dictionary<int, int>()
            {
                { Brow_Up_L,   Brow_Up_L_Value },
                { Brow_Up_C,   Brow_Up_C_Value },
                { Brow_Up_R,   Brow_Up_R_Value },

                { Brow_Down_L, Brow_Down_L_Value },
                { Brow_Down_C, Brow_Down_C_Value },
                { Brow_Down_R, Brow_Down_R_Value },

                { Cheek_L,     Cheek_L_Value },
                { Cheek_R,     Cheek_R_Value },

                { Mouth_Up,    Mouth_Up_Value },
                { Mouth_Down,  Mouth_Down_Value },
                { Mouth_L,     Mouth_L_Value },
                { Mouth_R,     Mouth_R_Value }
            };

            return values;
        }
    }

    [CreateAssetMenu(menuName = "VRR/Face Emote Asset")]
    public class FaceEmote_Asset : ScriptableObject
    {
        /// <summary>
        /// The emote asset defines our emotional face pose.
        /// We set a type for easier search and retrieval,
        /// and we have the option to either use the dfined blendshape values of the emote struct
        /// or the defined Vector2Int (x,y) positions of the blendshape ocontrollers. (to be added later)
        /// </summary>
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649

        [SerializeField] FaceEmote_Types EmoteType;

        [SerializeField] bool UsePropertyValues = true;

        // preset values for the blendshapes
        [SerializeField] public EmoteProperties Properties;
        
        // preset values for the blendshape controllers
        [SerializeField] Vector2Int EmoteCoordinates_L;
        [SerializeField] Vector2Int EmoteCoordinates_R;
        
        #pragma warning restore 0649
        #endregion

        #region GETTERS
        public string GetName()
        {
            return EmoteType.ToString();
        }

        public FaceEmote_Types GetFaceEmoteType()
        {
            return EmoteType;
        }

        public Vector2Int GetLeftEmoteCoordinates()
        {
            return EmoteCoordinates_L;
        }

        public Vector2Int GetRightEmoteCoordinates()
        {
            return EmoteCoordinates_R;
        }       

        public bool UsingProperties()
        {
            return UsePropertyValues;
        }

        #endregion
    }
}