// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Toolbox > Control Rig page (UI only).
 *
 * Flow: pick a skeletal mesh (and optionally a Blender .rig.json) -> the page shows the bone map from
 * ControlRigGenerator::MapBones so the user can check it -> Generate builds the Modular Rig (Core_ControlRig.cpp).
 * This file owns the widgets, the file dialog, and opening the new rig afterwards.
 */

#include "Menu_ControlRig.h"

#include "ControlRigBlueprintLegacy.h"
#include "Core_Common.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "MenuWidgets.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_ControlRig"

/** Builds the lines of the Detected Bones table from the bone map. */
namespace RigBonesUI
{
	/** Found = check icon, Warning/Error = missing bones, Dim = nothing to show (no icon, dimmed text). */
	enum class EStatus : uint8 { Found, Warning, Error, Dim };

	/** One table row: its status, the short text, and the full bone list for the tooltip. */
	struct FLine
	{
		EStatus Status = EStatus::Found;
		FString Text;
		FString ToolTip;
	};

	/** Most bone names a row lists before "+N more". */
	constexpr int32 MaxNamesShown = 6;

	const FSlateBrush* StatusIcon(EStatus Status)
	{
		switch (Status)
		{
		case EStatus::Found:	return FAppStyle::GetBrush("Icons.SuccessWithColor");
		case EStatus::Warning:	return FAppStyle::GetBrush("Icons.WarningWithColor");
		case EStatus::Error:	return FAppStyle::GetBrush("Icons.ErrorWithColor");
		default:				return FAppStyle::GetNoBrush();
		}
	}

	/** A single bone (root, pelvis, head). Missing is an error when the rig can't be built without it. */
	FLine Single(FName Bone, bool bRequired)
	{
		if (Bone.IsNone())
		{
			return { bRequired ? EStatus::Error : EStatus::Warning, TEXT("not found") };
		}
		return { EStatus::Found, Bone.ToString(), Bone.ToString() };
	}

	/** A list of bones: "3 bones: a, b, c", shortened past MaxNamesShown. Empty is a warning only when bExpected. */
	FLine List(const TArray<FName>& Names, bool bExpected, bool bShowCount)
	{
		if (Names.IsEmpty())
		{
			return { bExpected ? EStatus::Warning : EStatus::Dim, TEXT("none found") };
		}

		TArray<FString> All;
		for (const FName& Name : Names)
		{
			All.Add(Name.ToString());
		}

		TArray<FString> Shown(All.GetData(), FMath::Min(All.Num(), MaxNamesShown));
		FString Text = FString::Join(Shown, TEXT(", "));
		if (All.Num() > MaxNamesShown)
		{
			Text += FString::Printf(TEXT(", +%d more"), All.Num() - MaxNamesShown);
		}
		if (bShowCount)
		{
			Text = FString::Printf(TEXT("%d: %s"), All.Num(), *Text);
		}
		return { EStatus::Found, Text, FString::Join(All, TEXT("\n")) };
	}

	/** A limb chain "clavicle > upperarm > lowerarm > hand" (a dash for each missing bone), plus the twist count. */
	FLine Chain(std::initializer_list<FName> Bones, int32 NumTwist)
	{
		TArray<FString> Parts;
		int32 NumFound = 0;
		for (const FName& Bone : Bones)
		{
			Parts.Add(Bone.IsNone() ? FString(TEXT("-")) : Bone.ToString());
			NumFound += Bone.IsNone() ? 0 : 1;
		}

		FString Text = FString::Join(Parts, TEXT("  >  "));
		if (NumTwist > 0)
		{
			Text += FString::Printf(TEXT("   (%d twist)"), NumTwist);
		}
		if (NumFound == 0)
		{
			return { EStatus::Warning, TEXT("not found") };
		}
		return { NumFound == static_cast<int32>(Bones.size()) ? EStatus::Found : EStatus::Warning, Text, Text };
	}

	/** Finger names with their bone counts, or why there are none. */
	FLine Fingers(const FRigLimbBones& Limb, bool bIncluded)
	{
		if (!bIncluded)
		{
			return { EStatus::Dim, TEXT("not included") };
		}
		if (Limb.Fingers.IsEmpty())
		{
			return { EStatus::Dim, TEXT("none found") };
		}

		TArray<FString> Names, Details;
		for (const TPair<FString, TArray<FName>>& Finger : Limb.Fingers)
		{
			Names.Add(FString::Printf(TEXT("%s (%d)"), *Finger.Key, Finger.Value.Num()));

			TArray<FString> Chain;
			for (const FName& Bone : Finger.Value)
			{
				Chain.Add(Bone.ToString());
			}
			Details.Add(FString::Printf(TEXT("%s: %s"), *Finger.Key, *FString::Join(Chain, TEXT(" > "))));
		}
		return { EStatus::Found, FString::Join(Names, TEXT(", ")), FString::Join(Details, TEXT("\n")) };
	}

	/** Row for an option that is switched off in Include. */
	FLine NotIncluded()
	{
		return { EStatus::Dim, TEXT("not included") };
	}
}


/** Builds the page: character, detected bones, options, output and generate cards. */
void SMenu_ControlRig::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		VrrMenu::Stack({
			CreateMeshSection(),
			CreateBonesSection(),
			CreateOptionsSection(),
			CreateOutputSection(),
			CreateGenerateSection(),
		})
	];
}

// LAYOUT

/** Skeletal mesh picker, "Use Selected", and the rig description file row (browse/clear). */
TSharedRef<SWidget> SMenu_ControlRig::CreateMeshSection()
{
	auto IconButton = [](const FName Icon, const FText& ToolTip) -> TSharedRef<SButton>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(ToolTip)
			.ContentPadding(FMargin(2.0f))
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(Icon))
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	};

	TSharedRef<SButton> BrowseButton = IconButton("Icons.FolderOpen", LOCTEXT("BrowseRigDescriptionTip", "Pick a .rig.json file"));
	BrowseButton->SetOnClicked(FOnClicked::CreateSP(this, &SMenu_ControlRig::OnBrowseRigDescriptionClicked));

	TSharedRef<SButton> ClearButton = IconButton("Icons.X", LOCTEXT("ClearRigDescriptionTip", "Don't use a rig description (find bones by name)"));
	ClearButton->SetOnClicked(FOnClicked::CreateLambda([this]() { SetRigDescription(FString(), false); return FReply::Handled(); }));
	ClearButton->SetEnabled(TAttribute<bool>::CreateLambda([this]() { return !RigDescriptionPath.IsEmpty(); }));

	return VrrMenu::HelpCard(LOCTEXT("MeshHeader", "Character"), LOCTEXT("MeshInfo", "Builds a Modular Control Rig from Epic's rig modules: Root, Spine, Neck, Shoulder, Arm (IK/FK), Leg (IK/FK), Foot and Finger, plus simple FK controls for face and other bones."), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("MeshLabel", "Skeletal mesh"),
			VrrMenu::MeshPicker(VrrMenu::EMeshTypes::SkeletalOnly,
				[this]() { return Mesh.Get(); },
				[this](UObject* NewMesh) { SetMesh(Cast<USkeletalMesh>(NewMesh)); },
				[this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("RigDescription", "Blender rig"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(VrrMenu::WideWidth)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("RigDescriptionHint", "none - bones are found by name"))
					.Text_Lambda([this]() { return FText::FromString(RigDescriptionPath); })
					.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
					{
						FString Path = NewText.ToString().TrimStartAndEnd().TrimQuotes();
						if (Path != RigDescriptionPath)
						{
							SetRigDescription(Path, false);
						}
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				BrowseButton
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				ClearButton
			],
			LOCTEXT("RigDescriptionTip", "Optional rig description (.rig.json) written by Python/Blender/vrr_rig_exporter.py. When set, arms, legs and fingers come from the Blender rig's IK chains, and extra FK controls are made only for bones that have a control in Blender. A .rig.json next to the mesh (or one folder up) is picked automatically.")),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			return bRigDescriptionAuto && !RigDescriptionPath.IsEmpty()
				? LOCTEXT("RigDescriptionAuto", "Found automatically next to the mesh.")
				: FText::GetEmpty();
		})),
	}));
}

/** One row per body part (status icon + bones), the rig description notes and warnings, and a summary badge. */
TSharedRef<SWidget> SMenu_ControlRig::CreateBonesSection()
{
	using namespace RigBonesUI;

	// Label column + status icon + bones text; everything reads the current bone map each frame
	auto BoneRow = [](const FText& Label, TFunction<FLine()> GetLine) -> TSharedRef<SWidget>
	{
		return VrrMenu::Row(Label,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(SImage).Image_Lambda([GetLine]() { return StatusIcon(GetLine().Status); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([GetLine]() { return FText::FromString(GetLine().Text); })
				.ToolTipText_Lambda([GetLine]() { return FText::FromString(GetLine().ToolTip); })
				.ColorAndOpacity_Lambda([GetLine]()
				{
					return GetLine().Status == EStatus::Dim ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
				})
			]);
	};

	auto Side = [this](bool bLeft) -> const FRigLimbBones& { return bLeft ? Bones.Left : Bones.Right; };

	TSharedRef<SWidget> Table = VrrMenu::Rows({
		BoneRow(LOCTEXT("RootRow", "Root"), [this]() { return Single(Bones.Root, false); }),
		BoneRow(LOCTEXT("PelvisRow", "Pelvis"), [this]() { return Single(Bones.Pelvis, true); }),
		BoneRow(LOCTEXT("SpineRow", "Spine"), [this]() { return List(Bones.Spine, true, true); }),
		BoneRow(LOCTEXT("NeckRow", "Neck"), [this]() { return List(Bones.Neck, true, true); }),
		BoneRow(LOCTEXT("HeadRow", "Head"), [this]() { return Single(Bones.Head, false); }),
		BoneRow(LOCTEXT("LeftArmRow", "Left arm"), [Side]() { const FRigLimbBones& L = Side(true); return Chain({ L.Clavicle, L.UpperArm, L.LowerArm, L.Hand }, L.ArmTwist.Num()); }),
		BoneRow(LOCTEXT("RightArmRow", "Right arm"), [Side]() { const FRigLimbBones& L = Side(false); return Chain({ L.Clavicle, L.UpperArm, L.LowerArm, L.Hand }, L.ArmTwist.Num()); }),
		BoneRow(LOCTEXT("LeftLegRow", "Left leg"), [Side]() { const FRigLimbBones& L = Side(true); return Chain({ L.Thigh, L.Calf, L.Foot, L.Ball }, L.LegTwist.Num()); }),
		BoneRow(LOCTEXT("RightLegRow", "Right leg"), [Side]() { const FRigLimbBones& L = Side(false); return Chain({ L.Thigh, L.Calf, L.Foot, L.Ball }, L.LegTwist.Num()); }),
		BoneRow(LOCTEXT("LeftFingersRow", "Left fingers"), [this, Side]() { return Fingers(Side(true), Settings.bFingers); }),
		BoneRow(LOCTEXT("RightFingersRow", "Right fingers"), [this, Side]() { return Fingers(Side(false), Settings.bFingers); }),
		BoneRow(LOCTEXT("FaceRow", "Face"), [this]() { return Settings.bFace ? List(Bones.Face, false, true) : NotIncluded(); }),
		BoneRow(LOCTEXT("OtherRow", "Other"), [this]() { return Settings.bOther ? List(Bones.Other, false, true) : NotIncluded(); }),

		// Notes from the rig description, then warnings
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Join(Bones.Info, TEXT("\n"))); })),
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]() { return Bones.Warnings.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(VrrMenu::LabelWidth, 1.0f, 8.0f, 0.0f)
		[
			SNew(SImage).Image(FAppStyle::GetBrush("Icons.WarningWithColor"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]()
			{
				TArray<FString> Lines;
				for (const FText& Warning : Bones.Warnings)
				{
					Lines.Add(Warning.ToString());
				}
				return FText::FromString(FString::Join(Lines, TEXT("\n")));
			})
		],
	});
	Table->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return Mesh.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; }));

	// Summary in the title row: how many parts need a look
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VrrMenu::Badge(TAttribute<FText>::CreateLambda([this]()
			{
				const int32 Count = CountToCheck();
				return Mesh.IsValid() && Count > 0 ? FText::Format(LOCTEXT("ToCheckBadge", "{0} to check"), Count) : FText::GetEmpty();
			}), VrrMenu::WarningColor())
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VrrMenu::Badge(TAttribute<FText>::CreateLambda([this]()
			{
				return Mesh.IsValid() && Bones.CanGenerate() && CountToCheck() == 0 ? LOCTEXT("AllFoundBadge", "all found") : FText::GetEmpty();
			}), VrrMenu::SuccessColor())
		];

	return VrrMenu::Card(LOCTEXT("BonesHeader", "Detected Bones"), VrrMenu::Rows({
		SNew(SBox)
		.Visibility_Lambda([this]() { return Mesh.IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
		[
			VrrMenu::HelpText(LOCTEXT("NoMesh", "Pick a skeletal mesh to see which bones each rig module will use."))
		],
		Table,
	}), Summary);
}

/** Parts of the bone map that were not (fully) found: the warning rows of the table. */
int32 SMenu_ControlRig::CountToCheck() const
{
	using namespace RigBonesUI;

	const FLine Lines[] =
	{
		Single(Bones.Root, false), Single(Bones.Pelvis, true), List(Bones.Spine, true, true), List(Bones.Neck, true, true), Single(Bones.Head, false),
		Chain({ Bones.Left.Clavicle, Bones.Left.UpperArm, Bones.Left.LowerArm, Bones.Left.Hand }, 0),
		Chain({ Bones.Right.Clavicle, Bones.Right.UpperArm, Bones.Right.LowerArm, Bones.Right.Hand }, 0),
		Chain({ Bones.Left.Thigh, Bones.Left.Calf, Bones.Left.Foot, Bones.Left.Ball }, 0),
		Chain({ Bones.Right.Thigh, Bones.Right.Calf, Bones.Right.Foot, Bones.Right.Ball }, 0),
	};

	int32 Count = 0;
	for (const FLine& Line : Lines)
	{
		Count += Line.Status == EStatus::Warning || Line.Status == EStatus::Error ? 1 : 0;
	}
	return Count;
}

/** Which optional modules to add: fingers, face bones, other bones. */
TSharedRef<SWidget> SMenu_ControlRig::CreateOptionsSection()
{
	return VrrMenu::Card(LOCTEXT("OptionsHeader", "Include"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("ModulesLabel", "Also add"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				VrrMenu::CheckBox(&Settings.bFingers, LOCTEXT("Fingers", "Fingers"),
					LOCTEXT("FingersTip", "A Finger module per finger (thumb, index, middle, ring, pinky) on both hands"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				VrrMenu::CheckBox(&Settings.bFace, LOCTEXT("Face", "Face bones"),
					LOCTEXT("FaceTip", "An FK control for every bone under the head (jaw, eyes, eyelids, brows, lips...)"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				VrrMenu::CheckBox(&Settings.bOther, LOCTEXT("Other", "Other bones"),
					LOCTEXT("OtherTip", "An FK control for every remaining bone (backpack, shoulder pads, helper bones...). Twist, _end and IK_ bones are skipped. With a Blender rig description, only bones that have a control in Blender get one"))
			]),
	}));
}

/** Output folder, asset name, and "open when done". */
TSharedRef<SWidget> SMenu_ControlRig::CreateOutputSection()
{
	return VrrMenu::Card(LOCTEXT("OutputHeader", "Output"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("Name", "Name"),
			VrrMenu::NameField(&Settings.OutputName),
			LOCTEXT("NameTip", "A number is added if an asset with this name already exists; existing rigs are never overwritten")),
		VrrMenu::Row(LOCTEXT("WhenDone", "When done"),
			VrrMenu::CheckBox(&bOpenWhenDone, LOCTEXT("OpenWhenDone", "Open the rig"),
				LOCTEXT("OpenWhenDoneTip", "Open the new rig in the Control Rig editor to check it"))),
	}));
}

/** Generate button (enabled when the bone map can make a rig) and the status line. */
TSharedRef<SWidget> SMenu_ControlRig::CreateGenerateSection()
{
	return VrrMenu::Card(LOCTEXT("GenerateHeader", "Generate"), VrrMenu::Rows({
		VrrMenu::ActionButton(LOCTEXT("Generate", "Generate Control Rig"),
			LOCTEXT("GenerateTip", "Create a new Modular Control Rig asset for this mesh"),
			[this]()
			{
				if (!Mesh.IsValid())
				{
					return LOCTEXT("NeedMesh", "Pick a skeletal mesh first.");
				}
				return Bones.CanGenerate() ? FText::GetEmpty() : LOCTEXT("NeedPelvis", "No pelvis bone was found (see Detected Bones).");
			},
			FOnClicked::CreateSP(this, &SMenu_ControlRig::OnGenerateClicked)),
		VrrMenu::StatusLine(&StatusText, &bStatusIsError),
	}));
}

// BEHAVIOR
/** Switches to a new mesh: suggests the output, finds a rig description next to it (unless one was picked by hand), remaps bones. */
void SMenu_ControlRig::SetMesh(USkeletalMesh* NewMesh)
{
	Mesh = NewMesh;
	StatusText = FText::GetEmpty();

	if (NewMesh)
	{
		Settings.OutputFolder = FPackageName::GetLongPackagePath(NewMesh->GetOutermost()->GetName());
		Settings.OutputName = ControlRigGenerator::SuggestName(NewMesh);

		// Pick up a rig description saved with the mesh, unless one was chosen by hand
		if (RigDescriptionPath.IsEmpty() || bRigDescriptionAuto)
		{
			RigDescriptionPath = ControlRigGenerator::FindRigDescription(NewMesh);
			bRigDescriptionAuto = !RigDescriptionPath.IsEmpty();
		}
	}
	RefreshBones();
}

/** Uses this .rig.json (empty = none). bAuto marks it as found automatically, so a new mesh may replace it. */
void SMenu_ControlRig::SetRigDescription(const FString& Path, bool bAuto)
{
	RigDescriptionPath = Path;
	bRigDescriptionAuto = bAuto;
	StatusText = FText::GetEmpty();
	RefreshBones();
}

/** Re-runs the bone mapping for the current mesh and rig description. */
void SMenu_ControlRig::RefreshBones()
{
	Bones = ControlRigGenerator::MapBones(Mesh.Get(), RigDescriptionPath);
}

/** File dialog for a .rig.json, starting next to the mesh. */
FReply SMenu_ControlRig::OnBrowseRigDescriptionClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}
	FString DefaultFolder = RigDescriptionPath.IsEmpty() ? FPaths::ProjectContentDir() : FPaths::GetPath(RigDescriptionPath);
	if (RigDescriptionPath.IsEmpty() && Mesh.IsValid())
	{
		DefaultFolder = FPackageName::LongPackageNameToFilename(Settings.OutputFolder + TEXT("/"));
	}
	TArray<FString> Files;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (DesktopPlatform->OpenFileDialog(ParentWindow, LOCTEXT("PickRigDescription", "Pick a Blender rig description").ToString(),
		FPaths::ConvertRelativePathToFull(DefaultFolder), FString(), TEXT("Rig description (*.rig.json)|*.rig.json|JSON (*.json)|*.json"), EFileDialogFlags::None, Files)
		&& !Files.IsEmpty())
	{
		SetRigDescription(FPaths::ConvertRelativePathToFull(Files[0]), false);
	}
	return FReply::Handled();
}

/** Text (and error color) for the status line under the Generate button. */
void SMenu_ControlRig::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

/** Generates the rig, reports the result, then selects it and (optionally) opens its editor. */
FReply SMenu_ControlRig::OnGenerateClicked()
{
	const FControlRigGenResult Result = ControlRigGenerator::Generate(Mesh.Get(), Bones, Settings);

	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("Generated", "Control Rig generated") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error, 6.0f);

	if (Result.Blueprint)
	{
		VrrCore::ShowInContentBrowser({ Result.Blueprint });
		if (bOpenWhenDone && GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Result.Blueprint);
		}
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
