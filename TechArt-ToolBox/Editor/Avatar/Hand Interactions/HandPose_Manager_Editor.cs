using UnityEngine;
using UnityEditor;

namespace VRR.Avatar.Hands
{
    [CustomEditor(typeof(HandPose_Manager))]
    public class HandPoseManager_Editor : Editor
    {
        private HandPose_Manager _myTarget;
        SerializedObject _so;

        SerializedProperty HandPoseController;
        SerializedProperty HandInteraction;
        SerializedProperty AnimationSpeed;
        SerializedProperty Library;

        private void OnEnable()
        {
            if (_myTarget == null || _so == null)
            {
                GetProperties();
            }
        }

        private void GetProperties()
        {
            _myTarget   = (HandPose_Manager)target;
            _so         = new SerializedObject(_myTarget);

            HandPoseController  = _so.FindProperty("handPoseController");
            HandInteraction     = _so.FindProperty("handInteraction");
            AnimationSpeed      = _so.FindProperty("animationSpeed");
            Library             = _so.FindProperty("Library");
        }
        
        public override void OnInspectorGUI()
        {
            _so.Update();

            draw_menu();

            if (GUI.changed)
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }
       
        private void draw_menu()
        {
            if (Application.isPlaying)
            {
                EditorGUILayout.Space();
                
                _myTarget.HandPose = (HandPose_Types)EditorGUILayout.EnumPopup("Set Pose To: ", _myTarget.HandPose);

                EditorGUILayout.Space();
            }
            else
            {
                EditorGUILayout.PropertyField(HandPoseController, new GUIContent("Hand Pose Controller: "));
                EditorGUILayout.PropertyField(HandInteraction);
                EditorGUILayout.PropertyField(Library);
            }

            // EditorGUILayout.PropertyField(AnimationSpeed, new GUIContent("Animation Speed: "));
        }
    }
}