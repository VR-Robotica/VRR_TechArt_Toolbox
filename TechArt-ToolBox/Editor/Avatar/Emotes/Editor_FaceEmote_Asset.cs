using UnityEngine;
using UnityEditor;

using VRR.TechArtTools.Utilities;

namespace VRR.Avatar.Emote
{
    [CustomEditor(typeof(FaceEmote_Asset))]
    public class Editor_FaceEmote_Asset : Editor
    {
        FaceEmote_Asset _myTarget;
        SerializedObject _so;
        
        SerializedProperty EmoteType;

        SerializedProperty UsePropertyValues;
        SerializedProperty Properties;

        // *****
        // These values relate to the options available 
        // in the emote properties struct
        SerializedProperty SkinnedMesh;
        
        SerializedProperty Brow_Up_L,   Brow_Up_L_Value;
        SerializedProperty Brow_Up_C,   Brow_Up_C_Value;
        SerializedProperty Brow_Up_R,   Brow_Up_R_Value;

        SerializedProperty Brow_Down_L, Brow_Down_L_Value;
        SerializedProperty Brow_Down_C, Brow_Down_C_Value;
        SerializedProperty Brow_Down_R, Brow_Down_R_Value;

        SerializedProperty Cheek_L,     Cheek_L_Value;
        SerializedProperty Cheek_R,     Cheek_R_Value;

        SerializedProperty Mouth_Up,    Mouth_Up_Value;
        SerializedProperty Mouth_Down,  Mouth_Down_Value;
        SerializedProperty Mouth_L,     Mouth_L_Value;
        SerializedProperty Mouth_R,     Mouth_R_Value;
        // *****

        SerializedProperty EmoteCoordinates_L;
        SerializedProperty EmoteCoordinates_R;

        private string[] _blendshapes;

        private void OnEnable()
        {
            if (_myTarget == null || _so == null)
            {
                GetProperties();
            }
        }

        public override void OnInspectorGUI()
        {
            _so.Update();

            Draw_Menu();

            if (GUI.changed)
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }

        private void GetProperties()
        {
            _myTarget = (FaceEmote_Asset)target;

            _so = new SerializedObject(_myTarget);

            EmoteType           = _so.FindProperty("EmoteType");

            UsePropertyValues   = _so.FindProperty("UsePropertyValues");

            Properties          = _so.FindProperty("Properties");

            SkinnedMesh         = Properties.FindPropertyRelative("SkinnedMesh");

            Brow_Up_L           = Properties.FindPropertyRelative("Brow_Up_L");    Brow_Up_L_Value     = Properties.FindPropertyRelative("Brow_Up_L_Value");
            Brow_Up_C           = Properties.FindPropertyRelative("Brow_Up_C");    Brow_Up_C_Value     = Properties.FindPropertyRelative("Brow_Up_C_Value");
            Brow_Up_R           = Properties.FindPropertyRelative("Brow_Up_R");    Brow_Up_R_Value     = Properties.FindPropertyRelative("Brow_Up_R_Value");

            Brow_Down_L         = Properties.FindPropertyRelative("Brow_Down_L");  Brow_Down_L_Value   = Properties.FindPropertyRelative("Brow_Down_L_Value");
            Brow_Down_C         = Properties.FindPropertyRelative("Brow_Down_C");  Brow_Down_C_Value   = Properties.FindPropertyRelative("Brow_Down_C_Value");
            Brow_Down_R         = Properties.FindPropertyRelative("Brow_Down_R");  Brow_Down_R_Value   = Properties.FindPropertyRelative("Brow_Down_R_Value");

            Cheek_L             = Properties.FindPropertyRelative("Cheek_L");      Cheek_L_Value       = Properties.FindPropertyRelative("Cheek_L_Value");
            Cheek_R             = Properties.FindPropertyRelative("Cheek_R");      Cheek_R_Value       = Properties.FindPropertyRelative("Cheek_R_Value");

            Mouth_Up            = Properties.FindPropertyRelative("Mouth_Up");     Mouth_Up_Value      = Properties.FindPropertyRelative("Mouth_Up_Value");
            Mouth_Down          = Properties.FindPropertyRelative("Mouth_Down");   Mouth_Down_Value    = Properties.FindPropertyRelative("Mouth_Down_Value");
            Mouth_L             = Properties.FindPropertyRelative("Mouth_L");      Mouth_L_Value       = Properties.FindPropertyRelative("Mouth_L_Value");
            Mouth_R             = Properties.FindPropertyRelative("Mouth_R");      Mouth_R_Value       = Properties.FindPropertyRelative("Mouth_R_Value");
            

            EmoteCoordinates_L  = _so.FindProperty("EmoteCoordinates_L");
            EmoteCoordinates_R  = _so.FindProperty("EmoteCoordinates_R");
        }

        private void Draw_Menu()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.PropertyField(EmoteType, new GUIContent("Face Emote Type:"));

            EditorGUILayout.PropertyField(UsePropertyValues, new GUIContent("Use Property Struct Values? "));

            if (UsePropertyValues.boolValue == true)
            {
                EditorGUILayout.PropertyField(SkinnedMesh, new GUIContent("Mesh Reference:"), true);
                if (SkinnedMesh.objectReferenceValue != null)
                {
                    GetBlendshapeNames();
                    Draw_BlendshapeInputs();
                }
                else
                {
                    ClearBlendshapeNames();
                }
            }
            else
            {
                ClearBlendshapeNames();
                Draw_ControllerValues();
            }

            EditorGUILayout.EndVertical();
        }

        private void Draw_BlendshapeInputs()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("NOTE: Make sure you set all the blendshapes properly, the dictionary can't reference the same index more than once.", EditorStyles.wordWrappedLabel);

            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Eye Emote Blendshapes:", EditorStyles.boldLabel);
            Brow_Up_L.intValue          = EditorGUILayout.Popup("Brow Up-Left: ",      Brow_Up_L.intValue, _blendshapes);
            Brow_Up_L_Value.intValue    = EditorGUILayout.IntSlider(Brow_Up_L_Value.intValue, 0, 100);

            Brow_Up_C.intValue          = EditorGUILayout.Popup("Brow Up-Center: ",    Brow_Up_C.intValue, _blendshapes);
            Brow_Up_C_Value.intValue    = EditorGUILayout.IntSlider(Brow_Up_C_Value.intValue, 0, 100);

            Brow_Up_R.intValue          = EditorGUILayout.Popup("Brow Up-Right: ",     Brow_Up_R.intValue, _blendshapes);
            Brow_Up_R_Value.intValue    = EditorGUILayout.IntSlider(Brow_Up_R_Value.intValue, 0, 100);

            EditorGUILayout.EndVertical();
            EditorGUILayout.BeginVertical("Box");

            Brow_Down_L.intValue        = EditorGUILayout.Popup("Brow Down-Left: ",   Brow_Down_L.intValue, _blendshapes);
            Brow_Down_L_Value.intValue  = EditorGUILayout.IntSlider(Brow_Down_L_Value.intValue, 0, 100);

            Brow_Down_C.intValue        = EditorGUILayout.Popup("Brow Down-Center: ", Brow_Down_C.intValue, _blendshapes);
            Brow_Down_C_Value.intValue  = EditorGUILayout.IntSlider(Brow_Down_C_Value.intValue, 0, 100);

            Brow_Down_R.intValue        = EditorGUILayout.Popup("Brow Down-Right: ",  Brow_Down_R.intValue, _blendshapes);
            Brow_Down_R_Value.intValue  = EditorGUILayout.IntSlider(Brow_Down_R_Value.intValue, 0, 100);

            EditorGUILayout.EndVertical();
            EditorGUILayout.BeginVertical("Box");

            Cheek_L.intValue            = EditorGUILayout.Popup("Cheek Left: ",     Cheek_L.intValue,   _blendshapes);
            Cheek_L_Value.intValue      = EditorGUILayout.IntSlider(Cheek_L_Value.intValue, 0, 100);

            Cheek_R.intValue            = EditorGUILayout.Popup("Cheek Right: ",    Cheek_R.intValue,   _blendshapes);
            Cheek_R_Value.intValue      = EditorGUILayout.IntSlider(Cheek_R_Value.intValue, 0, 100);

            EditorGUILayout.EndVertical();
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Mouth Emote Blendshapes:", EditorStyles.boldLabel);

            Mouth_Up.intValue           = EditorGUILayout.Popup("Up: ",     Mouth_Up.intValue,  _blendshapes);
            Mouth_Up_Value.intValue     = EditorGUILayout.IntSlider(Mouth_Up_Value.intValue, 0, 100);

            Mouth_Down.intValue         = EditorGUILayout.Popup("Down: ",   Mouth_Down.intValue,_blendshapes);
            Mouth_Down_Value.intValue   = EditorGUILayout.IntSlider(Mouth_Down_Value.intValue, 0, 100);

            Mouth_L.intValue            = EditorGUILayout.Popup("Left: ",   Mouth_L.intValue,   _blendshapes);
            Mouth_L_Value.intValue      = EditorGUILayout.IntSlider(Mouth_L_Value.intValue, 0, 100);
                
            Mouth_R.intValue            = EditorGUILayout.Popup("Right: ",  Mouth_R.intValue,   _blendshapes);
            Mouth_R_Value.intValue      = EditorGUILayout.IntSlider(Mouth_R_Value.intValue, 0, 100);

            EditorGUILayout.EndVertical();

            EditorGUILayout.EndVertical();
        }

        private void Draw_ControllerValues()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("X,Y Coordinates:", EditorStyles.boldLabel);
            EmoteCoordinates_L.vector2IntValue = EditorGUILayout.Vector2IntField("Left: ", EmoteCoordinates_L.vector2IntValue);
            EmoteCoordinates_R.vector2IntValue = EditorGUILayout.Vector2IntField("Right: ", EmoteCoordinates_R.vector2IntValue);

            EditorGUILayout.EndVertical();
        }

        private void GetBlendshapeNames()
        {
            if (SkinnedMesh.objectReferenceValue != null)
            {
                _blendshapes = CustomUtilities.GetBlendshapeNames((SkinnedMeshRenderer)SkinnedMesh.objectReferenceValue);
            }
        }

        private void ClearBlendshapeNames()
        {
            _blendshapes = null;
        }
    }
}