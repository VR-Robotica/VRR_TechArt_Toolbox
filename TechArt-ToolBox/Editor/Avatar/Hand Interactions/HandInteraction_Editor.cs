using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEditor;

using VRR.Input;
using VRR.Interactions;

namespace VRR.Avatar.Hands
{
    [CustomEditor(typeof(HandInteraction))]
    public class HandInteraction_Editor : Editor
    {
        HandInteraction _myTarget;
        SerializedObject _so;

        SerializedProperty Handedness;
        SerializedProperty CollidedObjects;
        SerializedProperty CurrentVelocity;
        SerializedProperty CurrentAngularVelocity;

        #region MONOBEHAVIOR
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
            _myTarget = (HandInteraction)target;
            _so = new SerializedObject(_myTarget);

            Handedness = _so.FindProperty("_handedness");
            CollidedObjects = _so.FindProperty("_collidedObjects");
            CurrentVelocity = _so.FindProperty("_currentVelocity");
            CurrentAngularVelocity = _so.FindProperty("_currentAngularVelocity");
        }
        private void DrawMenu()
        {
            EditorGUILayout.PropertyField(Handedness);

            DrawMenu_Velocity();

            EditorGUILayout.Space();

            DrawMenu_ObjectInHand();

            EditorGUILayout.Space();

            DrawMenu_ObjectsWithinReach();
        }

        #region MENUS
        private void DrawMenu_ObjectsWithinReach()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Objects Within Reach: ", EditorStyles.boldLabel);

            foreach (GrabableObject obj in _myTarget._objectsWithinReach)
            {
                if (obj != null)
                {
                    EditorGUILayout.LabelField(obj.name);
                }
            }

            EditorGUILayout.EndVertical();
        }
        private void DrawMenu_ObjectInHand()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Object In Hand: ", EditorStyles.boldLabel);

            if (_myTarget.GetGrabbedObject() != null)
            {
                EditorGUILayout.LabelField(_myTarget.GetGrabbedObject().name);
                EditorGUILayout.LabelField(_myTarget.GetPose().ToString());
            }

            EditorGUILayout.EndVertical();
        }                       
        private void DrawMenu_Velocity()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Velocity: ", EditorStyles.boldLabel);
            EditorGUILayout.LabelField(CurrentVelocity.vector3Value.ToString());

            EditorGUILayout.LabelField("Angular Velocity: ", EditorStyles.boldLabel);
            EditorGUILayout.LabelField(CurrentAngularVelocity.vector3Value.ToString());

            EditorGUILayout.EndVertical();
        }
        #endregion
    }
}