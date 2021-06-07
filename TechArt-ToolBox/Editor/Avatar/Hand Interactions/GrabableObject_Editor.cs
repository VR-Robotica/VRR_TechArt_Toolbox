using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEditor;

using VRR.Input;

namespace VRR.Interactions
{
    [CustomEditor(typeof(GrabableObject))]
    public class GrabableObject_Editor : Editor
    {
        private GrabableObject _myTarget;
        SerializedObject _so;

        SerializedProperty PoseType;
        SerializedProperty PosOffest;
        SerializedProperty RotOffset;

        private Vector3 _startPosition;
        private Vector3 _startRotation;
        private bool    _editMode;

        #region MONOBEHAVIORS
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

            DrawMenu();

            if (GUI.changed)
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }
        #endregion

        private void GetProperties()
        {
            _myTarget = (GrabableObject)target;
            _so = new SerializedObject(_myTarget);

            PoseType = _so.FindProperty("_poseType");
            PosOffest = _so.FindProperty("_positionOffset");
            RotOffset = _so.FindProperty("_rotationOffset");
        }
        private void DrawMenu()
        {
            DrawMenu_PoseType();

            DrawMenu_Offsets();
        }

        #region MENUS
        private void DrawMenu_PoseType()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.PropertyField(PoseType);

            EditorGUILayout.EndVertical();
        }
        private void DrawMenu_Offsets()
        {
            EditorGUILayout.BeginVertical("Box");
            EditorGUILayout.LabelField("Transform Offsets while in the Hand", EditorStyles.boldLabel);

            if (_editMode)
            {
                EditorGUILayout.PropertyField(PosOffest);
                EditorGUILayout.PropertyField(RotOffset);

                DrawButton_Save();
            }
            else
            {
                EditorGUILayout.LabelField("Postion Offset: " + PosOffest.vector3Value.ToString() );
                EditorGUILayout.LabelField("Rotation Offset: " + RotOffset.vector3Value.ToString());

                DrawButton_EditMode();
            }
            EditorGUILayout.EndVertical();

            
        }
        private void DrawButton_EditMode()
        {
            EditorGUILayout.BeginVertical("Box");

            if (GUILayout.Button("EDIT MODE"))
            {
                SetEditMode(true);
            }

            EditorGUILayout.EndVertical();
        }
        private void DrawButton_Save()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Save Current Transform as Offset?", EditorStyles.boldLabel);

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("SAVE"))
            {
                _myTarget.Save();
                SetEditMode(false);
            }

            if (GUILayout.Button("CANCEL"))
            {
                SetEditMode(false);
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.EndVertical();
        }
        #endregion

        #region FUNCTIONS
        private void SetEditMode(bool value)
        {
            _editMode = value;

            // when setting up or editing grab position, use the right hand as the reference
            ControllerHand handedness = ControllerHand.Right;

            if (_editMode)
            {
                // get the start transforms of our object
                _startPosition = _myTarget.transform.localPosition;
                _startRotation = _myTarget.transform.localRotation.eulerAngles;

                // move object to offset settings
                _myTarget.transform.localPosition = _myTarget.GetPositionOffset(handedness);
                _myTarget.transform.localRotation = Quaternion.Euler(_myTarget.GetRotationOffset(handedness));
            }
            else
            {
                // move object back to original transforms
                _myTarget.transform.localPosition = _startPosition;
                _myTarget.transform.localRotation = Quaternion.Euler(_startRotation);
            }
        }
        #endregion
    }
}