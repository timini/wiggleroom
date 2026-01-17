#!/usr/bin/env python3
"""
Audio Analysis and Spectrogram Generator

Generates spectrograms from WAV files to visualize how DSP parameters
affect the sound.

Usage:
    python analyze_audio.py test.wav --output spectrogram.png
    python analyze_audio.py output/wav/ --grid --output comparison.png
"""

import argparse
import os
import sys
from pathlib import Path

import librosa
import librosa.display
import matplotlib.pyplot as plt
import numpy as np


def load_audio(filepath: str, sr: int = None) -> tuple[np.ndarray, int]:
    """Load audio file and return samples and sample rate."""
    y, sr = librosa.load(filepath, sr=sr, mono=False)
    if y.ndim == 1:
        y = y.reshape(1, -1)  # Ensure 2D (channels, samples)
    return y, sr


def generate_spectrogram(
    y: np.ndarray,
    sr: int,
    title: str = None,
    output_path: str = None,
    show: bool = False,
    figsize: tuple = (12, 6),
    use_mel: bool = True,
    n_fft: int = 2048,
    hop_length: int = 512,
) -> plt.Figure:
    """
    Generate a spectrogram from audio data.

    Args:
        y: Audio samples (channels, samples) or (samples,)
        sr: Sample rate
        title: Plot title
        output_path: Path to save the figure
        show: Whether to display the figure
        figsize: Figure size
        use_mel: Use mel spectrogram (True) or linear (False)
        n_fft: FFT window size
        hop_length: Hop length for STFT

    Returns:
        matplotlib Figure object
    """
    # Handle stereo by mixing to mono for spectrogram
    if y.ndim > 1:
        y_mono = np.mean(y, axis=0)
    else:
        y_mono = y

    # Compute spectrogram
    if use_mel:
        S = librosa.feature.melspectrogram(
            y=y_mono, sr=sr, n_fft=n_fft, hop_length=hop_length, n_mels=128
        )
        S_db = librosa.power_to_db(S, ref=np.max)
        ylabel = "Mel Frequency"
    else:
        S = np.abs(librosa.stft(y_mono, n_fft=n_fft, hop_length=hop_length))
        S_db = librosa.amplitude_to_db(S, ref=np.max)
        ylabel = "Frequency (Hz)"

    # Create figure
    fig, ax = plt.subplots(figsize=figsize)

    if use_mel:
        img = librosa.display.specshow(
            S_db, sr=sr, hop_length=hop_length, x_axis="time", y_axis="mel", ax=ax
        )
    else:
        img = librosa.display.specshow(
            S_db, sr=sr, hop_length=hop_length, x_axis="time", y_axis="hz", ax=ax
        )

    fig.colorbar(img, ax=ax, format="%+2.0f dB")
    ax.set(ylabel=ylabel)

    if title:
        ax.set_title(title)

    plt.tight_layout()

    if output_path:
        fig.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved spectrogram to {output_path}")

    if show:
        plt.show()

    return fig


def generate_comparison_grid(
    wav_files: list[str],
    output_path: str = None,
    show: bool = False,
    cols: int = 4,
    figsize_per_plot: tuple = (4, 3),
    use_mel: bool = True,
) -> plt.Figure:
    """
    Generate a grid of spectrograms for comparison.

    Args:
        wav_files: List of WAV file paths
        output_path: Path to save the figure
        show: Whether to display the figure
        cols: Number of columns in grid
        figsize_per_plot: Size of each subplot
        use_mel: Use mel spectrogram

    Returns:
        matplotlib Figure object
    """
    n = len(wav_files)
    rows = (n + cols - 1) // cols

    fig, axes = plt.subplots(
        rows, cols, figsize=(figsize_per_plot[0] * cols, figsize_per_plot[1] * rows)
    )
    axes = np.atleast_2d(axes)

    for idx, wav_file in enumerate(wav_files):
        row, col = divmod(idx, cols)
        ax = axes[row, col]

        try:
            y, sr = load_audio(wav_file)
            y_mono = np.mean(y, axis=0) if y.ndim > 1 else y

            if use_mel:
                S = librosa.feature.melspectrogram(y=y_mono, sr=sr, n_mels=64)
                S_db = librosa.power_to_db(S, ref=np.max)
                librosa.display.specshow(
                    S_db, sr=sr, x_axis="time", y_axis="mel", ax=ax
                )
            else:
                S = np.abs(librosa.stft(y_mono))
                S_db = librosa.amplitude_to_db(S, ref=np.max)
                librosa.display.specshow(
                    S_db, sr=sr, x_axis="time", y_axis="hz", ax=ax
                )

            # Use filename as title
            title = Path(wav_file).stem
            # Truncate long titles
            if len(title) > 30:
                title = title[:27] + "..."
            ax.set_title(title, fontsize=8)

        except Exception as e:
            ax.text(0.5, 0.5, f"Error:\n{e}", ha="center", va="center")
            ax.set_title(Path(wav_file).name, fontsize=8)

    # Hide empty subplots
    for idx in range(n, rows * cols):
        row, col = divmod(idx, cols)
        axes[row, col].axis("off")

    plt.tight_layout()

    if output_path:
        fig.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Saved comparison grid to {output_path}")

    if show:
        plt.show()

    return fig


def analyze_parameter_sweep(
    wav_dir: str,
    module_name: str,
    param_name: str,
    output_path: str = None,
    show: bool = False,
) -> plt.Figure:
    """
    Analyze a parameter sweep by finding all WAV files for a module/parameter
    and creating a comparison grid.

    Args:
        wav_dir: Directory containing WAV files
        module_name: Name of the module
        param_name: Name of the parameter being swept
        output_path: Path to save the figure
        show: Whether to display

    Returns:
        matplotlib Figure object
    """
    # Find matching files
    pattern = f"{module_name}_{param_name}_*.wav"
    wav_files = sorted(Path(wav_dir).glob(pattern))

    if not wav_files:
        print(f"No files matching {pattern} in {wav_dir}")
        return None

    print(f"Found {len(wav_files)} files for {module_name}/{param_name}")
    return generate_comparison_grid(
        [str(f) for f in wav_files],
        output_path=output_path,
        show=show,
        cols=min(5, len(wav_files)),
    )


def main():
    parser = argparse.ArgumentParser(
        description="Generate spectrograms from audio files"
    )
    parser.add_argument(
        "input", help="Input WAV file or directory of WAV files"
    )
    parser.add_argument(
        "-o", "--output", help="Output image file (PNG)"
    )
    parser.add_argument(
        "--grid", action="store_true", help="Generate comparison grid from directory"
    )
    parser.add_argument(
        "--linear", action="store_true", help="Use linear frequency scale instead of mel"
    )
    parser.add_argument(
        "--show", action="store_true", help="Display the figure"
    )
    parser.add_argument(
        "--cols", type=int, default=4, help="Number of columns in grid mode"
    )
    parser.add_argument(
        "--module", help="Module name for parameter sweep analysis"
    )
    parser.add_argument(
        "--param", help="Parameter name for parameter sweep analysis"
    )

    args = parser.parse_args()

    input_path = Path(args.input)

    # Parameter sweep mode
    if args.module and args.param:
        if not input_path.is_dir():
            print("Error: Input must be a directory for parameter sweep mode")
            sys.exit(1)
        fig = analyze_parameter_sweep(
            str(input_path),
            args.module,
            args.param,
            output_path=args.output,
            show=args.show,
        )
        if fig:
            plt.close(fig)
        return

    # Grid mode
    if args.grid or input_path.is_dir():
        if input_path.is_dir():
            wav_files = sorted(input_path.glob("*.wav"))
        else:
            print("Error: --grid requires a directory")
            sys.exit(1)

        if not wav_files:
            print(f"No WAV files found in {input_path}")
            sys.exit(1)

        fig = generate_comparison_grid(
            [str(f) for f in wav_files],
            output_path=args.output,
            show=args.show,
            cols=args.cols,
            use_mel=not args.linear,
        )
        plt.close(fig)
        return

    # Single file mode
    if not input_path.exists():
        print(f"Error: File not found: {input_path}")
        sys.exit(1)

    y, sr = load_audio(str(input_path))
    output = args.output or str(input_path.with_suffix(".png"))

    fig = generate_spectrogram(
        y,
        sr,
        title=input_path.name,
        output_path=output,
        show=args.show,
        use_mel=not args.linear,
    )
    plt.close(fig)


if __name__ == "__main__":
    main()
