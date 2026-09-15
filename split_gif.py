#!/usr/bin/env python3
"""
Split 320x160 GIF animations into two 160x160 GIFs from the middle

Usage:
    python split_gif.py --input <input_dir> --output <output_dir>
"""

import os
import argparse
from PIL import Image, ImageSequence

def split_gif(input_path, output_path):
    """Split a 320x160 GIF into two 160x160 GIFs from the middle"""
    try:
        # Open the original GIF
        with Image.open(input_path) as im:
            # Check if the image is a GIF
            if im.format != 'GIF':
                print(f"Skipping non-GIF file: {input_path}")
                return False
            
            # Check dimensions
            width, height = im.size
            if width != 320 or height != 160:
                print(f"Skipping GIF with wrong dimensions ({width}x{height}): {input_path}")
                return False
            
            # Get filename without extension
            filename = os.path.splitext(os.path.basename(input_path))[0]
            
            # Split point
            split_x = width // 2
            
            # Get original GIF info
            original_info = im.info
            
            # Process each frame
            frames_left = []
            frames_right = []
            durations = []
            
            for frame in ImageSequence.Iterator(im):
                # Preserve original palette if possible
                frame_copy = frame.copy()
                
                # Get frame duration
                try:
                    duration = frame.info['duration']
                except KeyError:
                    duration = 100  # Default duration if not specified
                durations.append(duration)
                
                # Split into left and right halves
                left_half = frame_copy.crop((0, 0, split_x, height))
                right_half = frame_copy.crop((split_x, 0, width, height))
                
                # Append to lists with original palette
                frames_left.append(left_half)
                frames_right.append(right_half)
            
            # Save parameters
            save_params = {
                'format': 'GIF',
                'save_all': True,
                'loop': original_info.get('loop', 0),
                'disposal': original_info.get('disposal', 2),
                'duration': durations,
                'optimize': False,
                'quality': 100,
                'dither': None,  # Disable dithering to preserve quality
            }
            
            # Add transparency if original has it
            if 'transparency' in original_info:
                save_params['transparency'] = original_info['transparency']
            
            # Save left half
            left_output = os.path.join(output_path, f"{filename}_left.gif")
            frames_left[0].save(
                left_output,
                append_images=frames_left[1:],
                **save_params
            )
            print(f"Saved left half: {left_output}")
            
            # Save right half
            right_output = os.path.join(output_path, f"{filename}_right.gif")
            frames_right[0].save(
                right_output,
                append_images=frames_right[1:],
                **save_params
            )
            print(f"Saved right half: {right_output}")
            
            return True
            
    except Exception as e:
        print(f"Error processing {input_path}: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Split 320x160 GIFs into two 160x160 GIFs')
    parser.add_argument('--input', '-i', default='GIF', help='Input directory containing GIF files')
    parser.add_argument('--output', '-o', default='split_gifs', help='Output directory for split GIFs')
    args = parser.parse_args()
    
    # Special case: if output is 'dual', create left and right directories
    if args.output == 'dual':
        # Create left and right directories
        left_dir = 'left'
        right_dir = 'right'
        os.makedirs(left_dir, exist_ok=True)
        os.makedirs(right_dir, exist_ok=True)
        
        # Get all GIF files in input directory
        gif_files = []
        for root, dirs, files in os.walk(args.input):
            for file in files:
                if file.lower().endswith('.gif'):
                    gif_files.append(os.path.join(root, file))
        
        if not gif_files:
            print(f"No GIF files found in {args.input}")
            return
        
        print(f"Found {len(gif_files)} GIF files, processing...")
        
        # Process each GIF and save to separate directories
        processed_count = 0
        for gif_file in gif_files:
            try:
                with Image.open(gif_file) as im:
                    # Check if the image is a GIF
                    if im.format != 'GIF':
                        print(f"Skipping non-GIF file: {gif_file}")
                        continue
                    
                    # Check dimensions
                    width, height = im.size
                    if width != 320 or height != 160:
                        print(f"Skipping GIF with wrong dimensions ({width}x{height}): {gif_file}")
                        continue
                    
                    # Get filename without extension
                    filename = os.path.splitext(os.path.basename(gif_file))[0]
                    
                    # Split point
                    split_x = width // 2
                    
                    # Get original GIF info
                    original_info = im.info
                    
                    # Process each frame
                    frames_left = []
                    frames_right = []
                    durations = []
                    
                    for frame in ImageSequence.Iterator(im):
                        # Preserve original palette if possible
                        frame_copy = frame.copy()
                        
                        # Get frame duration
                        try:
                            duration = frame.info['duration']
                        except KeyError:
                            duration = 100  # Default duration if not specified
                        durations.append(duration)
                        
                        # Split into left and right halves
                        left_half = frame_copy.crop((0, 0, split_x, height))
                        right_half = frame_copy.crop((split_x, 0, width, height))
                        
                        # Append to lists with original palette
                        frames_left.append(left_half)
                        frames_right.append(right_half)
                    
                    # Save parameters
                    save_params = {
                        'format': 'GIF',
                        'save_all': True,
                        'loop': original_info.get('loop', 0),
                        'disposal': original_info.get('disposal', 2),
                        'duration': durations,
                        'optimize': False,
                        'quality': 100,
                        'dither': None,  # Disable dithering to preserve quality
                    }
                    
                    # Add transparency if original has it
                    if 'transparency' in original_info:
                        save_params['transparency'] = original_info['transparency']
                    
                    # Save to left directory
                    left_output = os.path.join(left_dir, f"{filename}.gif")
                    frames_left[0].save(
                        left_output,
                        append_images=frames_left[1:],
                        **save_params
                    )
                    print(f"Saved left half: {left_output}")
                    
                    # Save to right directory
                    right_output = os.path.join(right_dir, f"{filename}.gif")
                    frames_right[0].save(
                        right_output,
                        append_images=frames_right[1:],
                        **save_params
                    )
                    print(f"Saved right half: {right_output}")
                    
                    processed_count += 1
            except Exception as e:
                print(f"Error processing {gif_file}: {e}")
                continue
        
        print(f"Processing complete. Successfully processed {processed_count} out of {len(gif_files)} GIFs.")
    else:
        # Original behavior: save to single output directory
        # Create output directory if it doesn't exist
        os.makedirs(args.output, exist_ok=True)
        
        # Get all GIF files in input directory
        gif_files = []
        for root, dirs, files in os.walk(args.input):
            for file in files:
                if file.lower().endswith('.gif'):
                    gif_files.append(os.path.join(root, file))
        
        if not gif_files:
            print(f"No GIF files found in {args.input}")
            return
        
        print(f"Found {len(gif_files)} GIF files, processing...")
        
        # Process each GIF
        processed_count = 0
        for gif_file in gif_files:
            if split_gif(gif_file, args.output):
                processed_count += 1
        
        print(f"Processing complete. Successfully processed {processed_count} out of {len(gif_files)} GIFs.")

if __name__ == "__main__":
    main()
